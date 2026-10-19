#include "dt/ps4_mapping.h"
#include <string.h>

_Static_assert(sizeof(ScePadData) == 120, "PS4 sample ABI size");
_Static_assert(offsetof(ScePadData, timestamp) == 80, "PS4 timestamp ABI offset");

void dt_ps4_neutral(ScePadData *out)
{
    memset(out, 0, sizeof(*out));
    out->lx = out->ly = out->rx = out->ry = 128;
    out->quat[3] = 1.0f;
    out->connected = 1;
}

void dt_ps4_mapping_reset(DtPs4Mapping *m)
{
    /* Preserve the ID sequence across reconnects, cancel everything else. */
    uint8_t finger = m->finger;
    memset(m, 0, sizeof(*m));
    m->finger = finger;
    m->blocked = 1;
}

static int axis(uint8_t value)
{
    int delta = (int)value - 128;
    if (delta > 16) return delta - 16;
    if (delta < -16) return delta + 16;
    return 0;
}

static int64_t move(int64_t position, int delta, uint64_t elapsed, int maximum)
{
    /* 1200 pixels/s at full deflection. Keep subpixel motion across frames. */
    int divisor = delta < 0 ? 112 : 111;
    position += (int64_t)delta * 1200 * (int64_t)elapsed / divisor;
    int64_t limit = (int64_t)maximum * 1000000;
    return position < 0 ? 0 : position > limit ? limit : position;
}

void dt_ps4_map(DtPs4Mapping *m, const DtControllerState *state,
                uint64_t now_us, ScePadData *out)
{
    /* Start remains Options; Select is arbitrated below. */
    static const uint32_t bits[17] = {
        0x00001, 0x00002, 0x00004, 0x00008, 0x00010, 0x00020,
        0x00040, 0x00080, 0x00100, 0x00200, 0x00400, 0x00800,
        0x01000, 0x02000, 0x04000, 0x08000, 0x10000
    };
    dt_ps4_neutral(out);
    for (unsigned i = 0; i < 17; ++i)
        if (state->buttons & (1u << i)) out->buttons |= bits[i];
    out->lx = state->lx; out->ly = state->ly;
    out->rx = state->rx; out->ry = state->ry;
    out->l2 = state->l2; out->r2 = state->r2;
    out->buttons &= ~1u;
    int select = (state->buttons & 1u) != 0;
    if (!(state->buttons & 4u)) m->r3_consumed = 0;
    else if (select) m->r3_consumed = 1;
    uint64_t elapsed = m->previous_us && now_us >= m->previous_us
        ? now_us - m->previous_us : 0;
    if (elapsed > 50000) elapsed = 50000; /* Do not jump after a scheduling gap. */
    m->previous_us = now_us;
    if (!select) m->blocked = 0;
    if (select && !m->blocked) {
        if (!m->held) {
            m->x = INT64_C(960000000); m->y = INT64_C(471000000);
            m->finger = (m->finger + 1) & 127;
            m->used = 0; m->share_until_us = 0;
            elapsed = 0;
        }
        m->held = 1;
        int dx = axis(state->rx), dy = axis(state->ry);
        if (dx || dy || (state->buttons & 4u)) m->used = 1;
        m->x = move(m->x, dx, elapsed, 1919);
        m->y = move(m->y, dy, elapsed, 941);
        out->touchData.fingers = 1;
        out->touchData.touch[0].finger = m->finger;
        out->touchData.touch[0].x = (uint16_t)(m->x / 1000000);
        out->touchData.touch[0].y = (uint16_t)(m->y / 1000000);
        if (state->buttons & 4u) out->buttons |= 0x100000u;
    } else if (m->held) {
        if (!m->used) m->share_until_us = now_us + 60000;
        m->held = 0;
    }
    /* Even a held Select awaiting release after recovery consumes its chord. */
    if (select) {
        out->rx = out->ry = 128;
    }
    if (m->r3_consumed) out->buttons &= ~4u;
    if (!select && now_us < m->share_until_us) out->buttons |= 1u;
}
