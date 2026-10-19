#include "dt/controller.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    /* Synthetic canonical HID frames, not captures from the user's DS3. */
    DtInputReport r = {.size = 49, .timestamp_us = 123456};
    DtControllerState s;
    r.data[0] = 1;
    r.data[6] = 128; r.data[7] = 128; r.data[8] = 128; r.data[9] = 128;
    assert(dt_ds3_decode(&r, &s) == 0);
    assert(s.buttons == 0 && s.lx == 128 && s.l2 == 0 && s.timestamp_us == 123456);
    for (unsigned bit = 0; bit < 17; ++bit) {
        memset(r.data + 2, 0, 3);
        r.data[2 + bit / 8] = (uint8_t)(1u << (bit % 8));
        assert(dt_ds3_decode(&r, &s) == 0 && s.buttons == (1u << bit));
    }
    r.data[6] = 0; r.data[7] = 255; r.data[8] = 7; r.data[9] = 248;
    r.data[18] = 37; r.data[19] = 201; r.data[30] = 0xee;
    r.data[41] = 0x02; r.data[42] = 0x01;
    assert(dt_ds3_decode(&r, &s) == 0);
    assert(s.lx == 0 && s.ly == 255 && s.rx == 7 && s.ry == 248);
    assert(s.l2 == 37 && s.r2 == 201 && s.pressure[4] == 37 && s.pressure[5] == 201);
    assert(s.battery_raw == 0xee && s.motion_raw[0] == 0x201);

    DtControllerState unchanged = s;
    for (size_t size = 0; size < 49; ++size) {
        r.size = size;
        assert(dt_ds3_decode(&r, &s) != 0 && memcmp(&s, &unchanged, sizeof(s)) == 0);
    }
    r.size = 49; r.data[0] = 0xa1; /* BT transport framing must be removed upstream. */
    assert(dt_ds3_decode(&r, &s) != 0);
    r.data[0] = 1; r.data[1] = 0xff; /* DS3 non-input packet. */
    assert(dt_ds3_decode(&r, &s) != 0);
    r.data[1] = 0; r.size = 129;
    assert(dt_ds3_decode(&r, &s) != 0);
    assert(dt_ds3_decode(NULL, &s) != 0 && dt_ds3_decode(&r, NULL) != 0);
    puts("PASS DS3: 17 buttons, independent analog triggers, axes, sensors and malformed frames");
}
