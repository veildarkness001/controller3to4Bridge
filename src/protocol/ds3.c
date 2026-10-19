#include "dt/controller.h"
#include <string.h>

int dt_ds3_decode(const DtInputReport *report, DtControllerState *out)
{
    DtControllerState decoded = {0};
    const uint8_t *p;
    if (!report || !out || report->size < 49 || report->size > sizeof(report->data))
        return -1;
    p = report->data;
    if (p[0] != 0x01 || p[1] == 0xff) return -1;
    decoded.buttons = p[2] | ((uint32_t)p[3] << 8) | ((uint32_t)(p[4] & 1) << 16);
    decoded.lx = p[6]; decoded.ly = p[7];
    decoded.rx = p[8]; decoded.ry = p[9];
    decoded.l2 = p[18]; decoded.r2 = p[19];
    memcpy(decoded.pressure, p + 14, sizeof(decoded.pressure));
    decoded.battery_raw = p[30];
    for (unsigned i = 0; i < 4; ++i)
        decoded.motion_raw[i] = ((uint16_t)p[41 + 2 * i] << 8) | p[42 + 2 * i];
    decoded.timestamp_us = report->timestamp_us;
    *out = decoded;
    return 0;
}
