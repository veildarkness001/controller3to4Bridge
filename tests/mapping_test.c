#include "dt/ps4_mapping.h"
#include <assert.h>
#include <stdio.h>

static DtControllerState state(void)
{
    return (DtControllerState){.lx=128, .ly=128, .rx=128, .ry=128};
}
static void released(DtPs4Mapping *m, uint64_t now)
{
    DtControllerState s = state(); ScePadData p;
    dt_ps4_map(m, &s, now, &p);
}
static void rate(unsigned period, ScePadData *out)
{
    DtPs4Mapping m = {0}; DtControllerState s = state();
    s.buttons = 1; dt_ps4_map(&m, &s, 100000, out);
    s.rx = 255;
    for (uint64_t t = 100000 + period; t <= 500000; t += period)
        dt_ps4_map(&m, &s, t, out);
}
int main(void)
{
    DtPs4Mapping m = {0}; ScePadData p;
    DtControllerState s = {.lx=1, .ly=254, .rx=7, .ry=249, .l2=37, .r2=201};
    for (unsigned bit = 1; bit < 17; ++bit) {
        s.buttons = 1u << bit;
        dt_ps4_map(&m, &s, 1000 + bit, &p);
        assert(p.buttons == (1u << bit));
        assert(p.lx == 1 && p.ly == 254 && p.rx == 7 && p.ry == 249);
        assert(p.l2 == 37 && p.r2 == 201 && p.connected && p.quat[3] == 1.0f);
        assert(!p.touchData.fingers && !p.timestamp && !p.count);
    }
    s = state(); s.buttons = 1;
    dt_ps4_map(&m, &s, 100000, &p);
    assert(!p.buttons && p.touchData.fingers == 1);
    assert(p.touchData.touch[0].x == 960 && p.touchData.touch[0].y == 471);
    uint8_t id = p.touchData.touch[0].finger;
    s.rx = 144; s.ry = 112; /* Dead zone must not consume Share. */
    dt_ps4_map(&m, &s, 110000, &p);
    assert(p.rx == 128 && p.ry == 128 && p.touchData.touch[0].x == 960);
    s.buttons = 0; dt_ps4_map(&m, &s, 120000, &p);
    assert(p.buttons == 1 && !p.touchData.fingers);
    dt_ps4_map(&m, &s, 179999, &p); assert(p.buttons == 1);
    dt_ps4_map(&m, &s, 180000, &p); assert(!p.buttons);
    s = state(); s.buttons = 5 | 0x4000 | 8;
    dt_ps4_map(&m, &s, 200000, &p);
    assert(p.buttons == (0x100000 | 0x4000 | 8)); /* Start still Options. */
    assert(p.touchData.fingers == 1 && p.touchData.touch[0].finger != id);
    id = p.touchData.touch[0].finger;
    s.buttons = 4; dt_ps4_map(&m, &s, 205000, &p);
    assert(!p.buttons && !p.touchData.fingers); /* Select released before R3. */
    s.buttons = 1; dt_ps4_map(&m, &s, 206000, &p);
    id = p.touchData.touch[0].finger;
    s.buttons = 5; dt_ps4_map(&m, &s, 207000, &p);
    s.buttons = 1; dt_ps4_map(&m, &s, 210000, &p);
    assert(!p.buttons && p.touchData.touch[0].finger == id);
    s.buttons = 0; dt_ps4_map(&m, &s, 220000, &p);
    assert(!p.buttons && !p.touchData.fingers); /* Click consumes Share. */
    ScePadData fast, slow; rate(4000, &fast); rate(10000, &slow);
    assert(fast.touchData.touch[0].x == 1440);
    assert(fast.touchData.touch[0].x == slow.touchData.touch[0].x);
    s = state(); s.buttons = 1; dt_ps4_map(&m, &s, 300000, &p);
    s.rx=255; s.ry=0;
    for (uint64_t t=310000; t<=2310000; t+=10000) dt_ps4_map(&m, &s, t, &p);
    assert(p.touchData.touch[0].x == 1919 && p.touchData.touch[0].y == 0);
    s.rx=0; s.ry=255;
    for (uint64_t t=2320000; t<=4320000; t+=10000) dt_ps4_map(&m, &s, t, &p);
    assert(p.touchData.touch[0].x == 0 && p.touchData.touch[0].y == 941);
    s.buttons = 0; dt_ps4_map(&m, &s, 4330000, &p); assert(!p.buttons);
    s = state(); s.buttons = 1; dt_ps4_map(&m, &s, 4400000, &p);
    s.rx=255; dt_ps4_map(&m, &s, 5400000, &p);
    assert(p.touchData.touch[0].x == 1020); /* Scheduling gap capped at 50ms. */
    dt_ps4_mapping_reset(&m);
    dt_ps4_map(&m, &s, 5500000, &p);
    assert(!p.buttons && !p.touchData.fingers && p.rx == 128);
    s.buttons = 0; dt_ps4_map(&m, &s, 5510000, &p); assert(!p.buttons);
    s = state(); s.buttons = 1; dt_ps4_map(&m, &s, 5520000, &p);
    assert(p.touchData.fingers == 1 && p.touchData.touch[0].x == 960);
    released(&m, 5530000); dt_ps4_mapping_reset(&m);
    s.buttons=0; dt_ps4_map(&m, &s, 5540000, &p); assert(!p.buttons);
    s.buttons = 0xfffe0000u; dt_ps4_map(&m, &s, 5600000, &p); assert(!p.buttons);
    dt_ps4_neutral(&p);
    assert(!p.buttons && p.lx == 128 && p.ly == 128 && p.rx == 128 && p.ry == 128);
    assert(!p.l2 && !p.r2 && !p.touchData.fingers && p.connected);
    puts("PASS PS4 mapping: button/analog preservation, touch contact/IDs, click, deferred Share, dead zone, time-based motion, bounds and loss cancellation");
}
