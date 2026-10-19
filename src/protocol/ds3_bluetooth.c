#include "dt/bluetooth.h"
#include <string.h>

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
