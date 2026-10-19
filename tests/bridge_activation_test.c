#include "dt/bridge_session.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { ABSENT, LATE_INPUT, READ_LOSS, STOP_DURING_READ, BAD_CLOCK, BACKWARD_CLOCK, STALE_INPUT };
static int scenario, attempts, reads, stop_requested;
static uint64_t now, last_attempt;
uint64_t dt_now_us(void) { return now; }
int dt_test_usleep(unsigned us) { assert(us == 1000); now += us; return 0; }
void dt_log(const char *fmt, ...) { (void)fmt; }
/* Waiting must never create, poll or send to a virtual controller. */
int dt_virtual_poll(DtVirtualPad *p) { (void)p; assert(0); return -1; }
int dt_virtual_send(DtVirtualPad *p, const ScePadData *s)
{ (void)p; (void)s; assert(0); return -1; }
static int stop(void *context) { (void)context; return stop_requested || now >= 4000000; }
static int reconnect(void *context)
{
    (void)context;
    if (last_attempt) assert(now - last_attempt >= 500000);
    last_attempt = now; ++attempts;
    return scenario == ABSENT ? 0 : 1;
}
static int read_report(void *context, DtInputReport *r, unsigned timeout)
{
    (void)context; assert(timeout == 20); ++reads; now += 20000;
    if (scenario == STOP_DURING_READ) stop_requested = 1;
    if (scenario == BAD_CLOCK) { now = 0; return 0; }
    if (scenario == BACKWARD_CLOCK) { now = 1; return 0; }
    if (scenario == READ_LOSS && reads == 1) return -1;
    if (scenario == STALE_INPUT && attempts == 1) return 0;
    if (scenario == LATE_INPUT && now < 1000000) {
        if (reads % 2) return 0;
        r->size = 7; return 1; /* Malformed input cannot activate the VDA. */
    }
    r->size = 49; r->data[0] = 1;
    /* Initial PS may already be released: valid neutral input is sufficient. */
    r->data[6] = r->data[7] = r->data[8] = r->data[9] = 128;
    return 1;
}
int main(void)
{
    const DtTransportOps ops = {.reconnect = reconnect, .read_report = read_report};
    DtTransport t = {.ops = &ops};
    DtBridgeConfig config = {.should_stop = stop};
    for (scenario = ABSENT; scenario <= STALE_INPUT; ++scenario) {
        now = 100; attempts = reads = stop_requested = 0; last_attempt = 0;
        int result = dt_bridge_wait_for_input(&t, &config);
        if (scenario == ABSENT) assert(result == 0 && reads == 0 && attempts == 8);
        else if (scenario == STOP_DURING_READ) assert(result == 0 && reads == 1);
        else if (scenario == BAD_CLOCK || scenario == BACKWARD_CLOCK) assert(result == -1);
        else {
            assert(result == 1);
            if (scenario == LATE_INPUT) assert(now >= 1000000 && attempts == 1);
            if (scenario == READ_LOSS) assert(attempts == 2 && reads == 2);
            if (scenario == STALE_INPUT) assert(attempts == 2 && now > 2500000);
        }
    }
    now = 100; stop_requested = 1; reads = attempts = 0;
    assert(dt_bridge_wait_for_input(&t, &config) == 0 && !reads && !attempts);
    scenario = ABSENT; now = 100; stop_requested = 0; last_attempt = 0;
    config = (DtBridgeConfig){.max_duration_us = 10000};
    assert(dt_bridge_wait_for_input(&t, &config) == 0 && now == 10100);
    assert(dt_bridge_wait_for_input(NULL, &config) == -1);
    puts("PASS deferred activation: absent/invalid input, released PS, stop during read, loss/retry, stale input, clocks and timeout; no VDA calls");
}
