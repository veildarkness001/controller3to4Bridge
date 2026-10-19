#include "dt/bridge_session.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { RECOVER, SILENT, INVALID, ABSENT, LONG, CYCLES, SEND_FAIL, LOGOUT, POLL_FAIL, CLOCK_FAIL, POWER_OFF };
static int scenario, connected, attempts, heartbeats, neutral_seen, resumed, errors;
static uint64_t now, end, last_attempt, last_send, maximum_gap, drop_at;
static unsigned sends, acquisition_reads, touches;
uint64_t dt_now_us(void) { return scenario == CLOCK_FAIL && now > 100000 ? 0 : now; }
int dt_test_usleep(unsigned us)
{
    assert(us > 0);
    now += scenario == LONG ? 10000 : us;
    return 0;
}
void dt_log(const char *format, ...)
{
    if (strstr(format, "session heartbeat")) ++heartbeats;
}
static int stop(void *context) { (void)context; return now >= end; }
static int reconnect(void *context)
{
    (void)context;
    ++attempts;
    if (last_attempt) assert(now - last_attempt >= 500000);
    last_attempt = now;
    if (scenario == ABSENT) return attempts % 2 ? 0 : -1;
    if (scenario == RECOVER && now >= 1000000 && now < 3000000) return 0;
    connected = 1;
    return 1;
}
static int read_report(void *context, DtInputReport *r, unsigned timeout)
{
    (void)context; assert(connected && (timeout == 4 || timeout == 20));
    if (timeout == 20) ++acquisition_reads;
    if (scenario == RECOVER && now >= 1000000 && now < 3000000) {
        connected = 0; ++errors; return -4;
    }
    if (scenario == CYCLES && now >= drop_at) {
        connected = 0; ++errors; drop_at = now + 700000; return -4;
    }
    if ((scenario == SILENT || scenario == INVALID) && now >= 1000000) {
        if (scenario == SILENT) return 0;
        r->size = 7; return 1;
    }
    /* On reconnect keep the old held state unavailable until first valid frame. */
    if (scenario == RECOVER && now >= 3000000 && now < 4200000) return 0;
    r->size = 49; r->data[0] = 1;
    r->data[3] = 0x40; /* Cross, before and after reconnection. */
    if ((scenario == RECOVER || scenario == SILENT || scenario == INVALID) &&
        now >= 20000 && now < 4300000) r->data[2] = 1; /* Held touch through loss. */
    r->data[6] = 255; r->data[7] = r->data[8] = r->data[9] = 128;
    r->timestamp_us = now;
    return 1;
}
int dt_virtual_poll(DtVirtualPad *p)
{
    (void)p;
    if (now > 1000000) {
        if (scenario == LOGOUT) return 1;
        if (scenario == POLL_FAIL) return -1;
        if (scenario == POWER_OFF) return DT_VIRTUAL_POWER_OFF;
    }
    return 0;
}
int dt_virtual_send(DtVirtualPad *p, const ScePadData *s)
{
    (void)p; assert(s->connected);
    ++sends;
    if (s->touchData.fingers) ++touches;
    if (last_send && now - last_send > maximum_gap) maximum_gap = now - last_send;
    last_send = now;
    if (scenario == SEND_FAIL) return 5; /* All nonzero results are errors. */
    if (scenario == RECOVER && now >= 1000000 && now < 4200000) {
        assert(s->buttons == 0 && s->lx == 128 && s->l2 == 0 && !s->touchData.fingers); ++neutral_seen;
    }
    if ((scenario == SILENT || scenario == INVALID) && now >= 1250000)
        assert(s->buttons == 0 && s->lx == 128 && !s->touchData.fingers);
    if (scenario == RECOVER && now >= 4200000) {
        assert(s->buttons == 0x4000 && !s->touchData.fingers); ++resumed;
    }
    if (scenario == ABSENT) assert(s->buttons == 0 && s->lx == 128);
    return 0;
}
int main(void)
{
    const DtTransportOps ops = {.read_report = read_report, .reconnect = reconnect};
    DtTransport t = {.ops = &ops};
    for (scenario = RECOVER; scenario <= POWER_OFF; ++scenario) {
        now = 1; end = 7000000; last_attempt = last_send = maximum_gap = 0;
        attempts = sends = heartbeats = neutral_seen = resumed = errors = acquisition_reads = 0;
        touches = 0;
        drop_at = 1000000;
        if (scenario == LONG) end = UINT64_C(8) * 60 * 60 * 1000000;
        if (scenario == CYCLES) end = UINT64_C(200) * 1000000;
        if (scenario == ABSENT) end = 60000000;
        DtBridgeConfig config = {.should_stop = stop};
        DtBridgeStats stats;
        int ret = dt_bridge_run(&t, (DtVirtualPad *)1, &config, &stats);
        if (scenario == SEND_FAIL) { assert(ret == 4 && !stats.sent); continue; }
        if (scenario == POLL_FAIL) { assert(ret == 3); continue; }
        if (scenario == CLOCK_FAIL) { assert(ret == 1); continue; }
        if (scenario == POWER_OFF) {
            assert(ret == DT_BRIDGE_POWER_OFF && now < end && stats.sent == sends);
            assert(now > 1000000 && now < 1002000);
            continue;
        }
        assert(ret == 0 && stats.sent == sends);
        if (scenario == RECOVER) {
            assert(stats.connections == 2 && stats.read_errors == 1);
            assert(neutral_seen > 500 && resumed > 100);
            assert(acquisition_reads > 1000);
            assert(touches > 100);
        }
        if (scenario == SILENT || scenario == INVALID) {
            assert(stats.stale >= 2 && touches > 100);
        }
        if (scenario == INVALID) assert(stats.invalid > 1000);
        if (scenario == ABSENT) assert(!stats.connections && attempts < 20);
        if (scenario == CYCLES) assert(stats.connections > 200 && stats.read_errors > 200);
        if (scenario == LONG) {
            assert(stats.connections == 1 && !stats.stale && !stats.read_errors);
            assert(stats.elapsed_us >= UINT64_C(8) * 60 * 60 * 1000000);
            assert(heartbeats == 479 && maximum_gap <= 10000);
            assert(acquisition_reads == 1);
        }
        /* Even unplugged we keep sending a neutral virtual sample. */
        assert(maximum_gap <= 10000);
    }
    /* Explicit finite test window remains available without a production limit. */
    scenario = LONG; now = 1; last_attempt = last_send = 0;
    DtBridgeConfig config = {.max_duration_us = 20000}; DtBridgeStats stats;
    assert(dt_bridge_run(&t, (DtVirtualPad *)1, &config, &stats) == 0);
    assert(stats.elapsed_us == 20000);
    puts("PASS bridge session: 8 simulated hours, >200 disconnects, absent/startup/backoff, stale/invalid neutralization, recovery, stop/logout/clock/output failures");
}
