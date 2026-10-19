#include "dt/bluetooth.h"
#include <string.h>

size_t dt_ds3_bluetooth_player_led(uint8_t *out, size_t capacity,
                                  DtBluetoothFraming framing, unsigned player)
{
    size_t prefix;
    if (framing == DT_BT_HID_REPORT) prefix = 0;
    else if (framing == DT_BT_HIDP_SDU) prefix = 1;
    else return 0;
    if (!out || player < 1 || player > 4 || capacity < 49 + prefix) return 0;
    /* DS3 output layout checked against USB Host Shield PS3BT/PS3Enums:
     * 48 payload bytes, mask at payload[9], four 5-byte LED timing fields.
     * Zero off-time requests steady illumination; all motor bytes stay zero. */
    memset(out, 0, 49 + prefix);
    if (prefix) out[0] = 0x52; /* SET_REPORT | Output, control channel. */
    uint8_t *report = out + prefix;
    report[0] = 1;
    report[10] = (uint8_t)(1u << player);
    for (unsigned i = 0; i < 4; ++i) {
        size_t offset = 11 + 5 * i;
        report[offset] = 0xff;
        report[offset + 1] = 0x27;
        report[offset + 2] = 0x10;
        report[offset + 4] = 0x32;
    }
    return 49 + prefix;
}

int dt_ds3_bluetooth_unpack(const uint8_t *data, size_t size,
                            DtBluetoothFraming framing, uint64_t timestamp_us,
                            DtInputReport *out)
{
    if (!data || !out) return -1;
    if (framing == DT_BT_HIDP_SDU) {
        if (!size || data[0] != 0xa1) return -1; /* DATA | Input */
        ++data; --size;
    } else if (framing != DT_BT_HID_REPORT) return -1;
    if (size < 49 || size > sizeof(out->data)) return -1;
    DtInputReport candidate = {.size = size, .timestamp_us = timestamp_us};
    memcpy(candidate.data, data, size);
    DtControllerState state;
    if (dt_ds3_decode(&candidate, &state) != 0) return -1;
    *out = candidate;
    return 0;
}

size_t dt_ds3_bluetooth_enable(uint8_t *out, size_t capacity,
                              DtBluetoothFraming framing)
{
    /* SDL HIDAPI PS3 + USB Host Shield PS3BT: F4 42 03 00 00.
     * USB uses 42 0c instead; never share its initialization command. */
    static const uint8_t command[] = {0x53, 0xf4, 0x42, 0x03, 0, 0};
    size_t offset;
    if (framing == DT_BT_HID_REPORT) offset = 1;
    else if (framing == DT_BT_HIDP_SDU) offset = 0;
    else return 0;
    size_t size = sizeof(command) - offset;
    if (!out || capacity < size) return 0;
    memcpy(out, command + offset, size);
    return size;
}
