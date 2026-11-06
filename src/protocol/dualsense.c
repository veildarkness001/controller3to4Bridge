#include "dt/dualsense.h"
#include <string.h>

/* Protocol facts checked against Linux hid-playstation v6.12 and SDL 3.2.0.
 * This parser is independent of either driver implementation. */
static uint32_t crc_byte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
    return crc;
}

int dt_dualsense_check_crc(uint8_t seed, const uint8_t *data, size_t size)
{
    if (!data || size < 4) return -1;
    uint32_t crc = crc_byte(UINT32_MAX, seed), expected = 0;
    for (size_t i = 0; i < size - 4; ++i) crc = crc_byte(crc, data[i]);
    for (unsigned i = 0; i < 4; ++i) expected |= (uint32_t)data[size-4+i] << (8*i);
    return ~crc == expected ? 0 : -1;
}

int dt_dualsense_decode(const DtInputReport *report, DtControllerState *out)
{
    if (!report || !out) return -1;
    const uint8_t *p;
    uint8_t basic[63] = {0};
    if (report->format == DT_REPORT_DUALSENSE_USB &&
        report->size == 64 && report->data[0] == 1) p = report->data + 1;
    else if (report->format == DT_REPORT_DUALSENSE_BT &&
             report->size == 78 && report->data[0] == 0x31) {
        if (dt_dualsense_check_crc(0xa1, report->data, report->size) != 0) return -1;
        p = report->data + 2;
    } else if (report->format == DT_REPORT_DUALSENSE_BT &&
               report->size == 10 && report->data[0] == 1) {
        /* Basic Bluetooth mode has no CRC, touch positions or sensors.
         * Only a backend that identified the Sony DualSense may select it. */
        memcpy(basic, report->data + 1, 4);
        basic[4] = report->data[8]; basic[5] = report->data[9];
        basic[7] = report->data[5]; basic[8] = report->data[6];
        basic[9] = report->data[7] & 3;
        basic[32] = basic[36] = 0x80;
        p = basic;
    } else return -1;
    DtControllerState s = {0};
    s.lx = p[0]; s.ly = p[1]; s.rx = p[2]; s.ry = p[3];
    s.l2 = p[4]; s.r2 = p[5];
    /* Semantic positions shared with DS3, not the DualSense wire bitmap. */
    static const uint8_t face[] = {15, 14, 13, 12};
    static const uint8_t shoulders[] = {10, 11, 8, 9, 0, 3, 1, 2};
    for (unsigned i = 0; i < 4; ++i)
        if (p[7] & (0x10u << i)) s.buttons |= 1u << face[i];
    for (unsigned i = 0; i < 8; ++i)
        if (p[8] & (1u << i)) s.buttons |= 1u << shoulders[i];
    unsigned hat = p[7] & 15;
    if (hat == 7 || hat == 0 || hat == 1) s.buttons |= 1u << 4;
    if (hat >= 1 && hat <= 3) s.buttons |= 1u << 5;
    if (hat >= 3 && hat <= 5) s.buttons |= 1u << 6;
    if (hat >= 5 && hat <= 7) s.buttons |= 1u << 7;
    if (p[9] & 1) s.buttons |= 1u << 16;
    s.native_touch = 1;
    s.touch_click = (p[9] >> 1) & 1;
    s.mute_button = (p[9] >> 2) & 1;
    for (unsigned i = 0; i < 2; ++i) {
        const uint8_t *t = p + 32 + 4*i;
        if (t[0] & 0x80) continue;
        unsigned x = t[1] | ((unsigned)(t[2] & 15) << 8);
        unsigned y = (t[2] >> 4) | ((unsigned)t[3] << 4);
        if (x >= 1920 || y >= 1080) return -1;
        unsigned n = s.touch_count++;
        s.touch[n].id = t[0] & 127;
        s.touch[n].x = (uint16_t)x;
        s.touch[n].y = (uint16_t)(y * 941 / 1079);
    }
    for (unsigned i = 0; i < 3; ++i) {
        unsigned g = p[15+2*i] | ((unsigned)p[16+2*i] << 8);
        unsigned a = p[21+2*i] | ((unsigned)p[22+2*i] << 8);
        s.gyro_raw[i] = (int16_t)(g < 32768 ? (int)g : (int)g - 65536);
        s.accel_raw[i] = (int16_t)(a < 32768 ? (int)a : (int)a - 65536);
    }
    s.battery_raw = p[52];
    s.timestamp_us = report->timestamp_us;
    *out = s;
    return 0;
}

int dt_dualsense_bluetooth_unpack(const uint8_t *data, size_t size,
                                  DtBluetoothFraming framing, uint64_t timestamp_us,
                                  DtInputReport *out)
{
    if (!data || !out || !timestamp_us) return -1;
    if (framing == DT_BT_HIDP_SDU) {
        if (!size || *data != 0xa1) return -1;
        ++data; --size;
    } else if (framing != DT_BT_HID_REPORT) return -1;
    if (size != 78 && size != 10) return -1;
    DtInputReport report = {.size = size, .timestamp_us = timestamp_us,
                            .format = DT_REPORT_DUALSENSE_BT};
    memcpy(report.data, data, size);
    DtControllerState state;
    if (dt_dualsense_decode(&report, &state) != 0) return -1;
    *out = report;
    return 0;
}

size_t dt_dualsense_bluetooth_enable(uint8_t *out, size_t capacity)
{
    if (!out || capacity < 78) return 0;
    /* Zero feature flags request enhanced mode without rumble, LEDs, audio
     * or adaptive trigger effects (SDL HIDAPI PS5 mode-entry reference). */
    memset(out, 0, 78); out[0] = 0x31; out[2] = 0x10;
    uint32_t crc = crc_byte(UINT32_MAX, 0xa2);
    for (unsigned i = 0; i < 74; ++i) crc = crc_byte(crc, out[i]);
    crc = ~crc;
    for (unsigned i = 0; i < 4; ++i) out[74+i] = (uint8_t)(crc >> (8*i));
    return 78;
}
