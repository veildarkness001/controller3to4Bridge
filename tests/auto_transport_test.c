#include "dt/auto_transport.h"
#include "dt/bridge_session.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t now;
static int mode, usb_reads, bt_reads, usb_attempts, bt_attempts, neutral, usb_samples, bt_samples;
static int bt_live, quiesces, fault;
enum { SWITCH, BOTH, USB_ONLY, BT_ONLY, ABSENT, SILENT_USB, PROBE_ERROR, QUIESCE_FAIL };
uint64_t dt_now_us(void) { return now; }
int dt_test_usleep(unsigned us) { now += us; return 0; }
void dt_log(const char *fmt, ...) { (void)fmt; }
static int usb_available(void)
{ return mode == BOTH || mode == USB_ONLY || mode == SILENT_USB || mode == QUIESCE_FAIL ||
         (mode == SWITCH && (now < 1000000 || now >= 2000000)); }
static int usb_present(DtTransport *t)
{ assert(t->kind == DT_TRANSPORT_USB); return mode == PROBE_ERROR ? -1 : usb_available(); }
static int connect_usb(void *c) { (void)c; assert(!bt_live); ++usb_attempts; return usb_available(); }
static int connect_bt(void *c)
{ (void)c; ++bt_attempts; bt_live = mode != ABSENT && mode != USB_ONLY; return bt_live; }
static int quiesce(DtTransport *t)
{ assert(t->kind == DT_TRANSPORT_BLUETOOTH); ++quiesces; if(mode == QUIESCE_FAIL) {fault=1;return -1;} bt_live=0;return 0; }
static int input(int usb, DtInputReport *r, unsigned timeout)
{
    assert(timeout == 4 || timeout == 20);
    if (usb) {
        ++usb_reads;
        if (!usb_available()) return -1;
        if (mode == SILENT_USB) return 0;
    } else ++bt_reads;
    r->size = 49; r->data[0] = 1;
    r->data[3] = usb ? 0x40 : 0x20; /* Cross vs Circle: distinguish sources. */
    r->data[6] = r->data[7] = r->data[8] = r->data[9] = 128;
    /* A held Select crossing a source boundary must not synthesize Share/click. */
    if (mode == SWITCH && now >= 900000) { r->data[2] = 1; r->data[8] = 255; }
    r->timestamp_us = now;
    return 1;
}
static int read_usb(void *c, DtInputReport *r, unsigned t) { (void)c; return input(1, r, t); }
static int read_bt(void *c, DtInputReport *r, unsigned t) { (void)c; return input(0, r, t); }
static int stop(void *c) { (void)c; return fault || now >= 4000000; }
int dt_virtual_poll(DtVirtualPad *p) { assert(p == (DtVirtualPad *)1); return 0; }
int dt_virtual_send(DtVirtualPad *p, const ScePadData *s)
{
    assert(p == (DtVirtualPad *)1);
    assert(!(s->buttons & 1)); /* No synthetic Share after source loss. */
    if (s->buttons & 0x4000) ++usb_samples;
    if (s->buttons & 0x2000) ++bt_samples;
    assert((s->buttons & 0x6000) != 0x6000);
    if (!s->buttons) ++neutral;
    if (mode == SWITCH && now >= 1000000 && now < 1500000)
        assert(!s->buttons && !s->touchData.fingers && s->rx == 128);
    if (mode == SWITCH && now >= 1500000) assert(!s->touchData.fingers);
    return 0;
}
int main(void)
{
    const DtTransportOps uops = {.read_report = read_usb, .reconnect = connect_usb};
    const DtTransportOps bops = {.read_report = read_bt, .reconnect = connect_bt};
    DtTransport usb = {.kind = DT_TRANSPORT_USB, .ops = &uops};
    DtTransport bt = {.kind = DT_TRANSPORT_BLUETOOTH, .ops = &bops};
    for (mode = SWITCH; mode <= QUIESCE_FAIL; ++mode) {
        now = 1; usb_reads = bt_reads = usb_attempts = bt_attempts = 0;
        neutral = usb_samples = bt_samples = 0;
        bt_live = quiesces = fault = 0;
        DtAutoTransport selection; DtTransport t;
        assert(dt_auto_transport_init(&t, &selection, mode == BT_ONLY ? NULL : &usb,
                                     mode == USB_ONLY ? NULL : &bt, usb_present, quiesce) == 0);
        const DtBridgeConfig config = {.should_stop = stop};
        if (mode == ABSENT || mode == QUIESCE_FAIL) {
            assert(dt_bridge_wait_for_input(&t, &config) == 0);
            assert(!usb_reads && !bt_reads);
            if(mode == QUIESCE_FAIL)assert(fault && !usb_attempts && !bt_attempts);
            continue;
        }
        DtBridgeStats stats;
        if (mode != SILENT_USB) assert(dt_bridge_wait_for_input(&t, &config) == 1);
        assert(dt_bridge_run(&t, (DtVirtualPad *)1, &config, &stats) == 0);
        if (mode == SWITCH) {
            assert(usb_samples && bt_samples && neutral);
            assert(stats.connections == 3 && stats.read_errors == 2);
            assert(quiesces >= 2);
        }
        if (mode == BOTH || mode == USB_ONLY) assert(usb_samples && !bt_reads);
        if (mode == BT_ONLY || mode == PROBE_ERROR) assert(bt_samples && !usb_reads);
        if (mode == SILENT_USB) assert(!usb_samples && !bt_samples && stats.stale);
        t.ops->close(t.context); assert(!selection.active);
    }
    DtTransport t; DtAutoTransport s;
    assert(dt_auto_transport_init(&t, &s, NULL, NULL, NULL, NULL) < 0);
    assert(dt_auto_transport_init(&t, &s, &bt, &usb, NULL, NULL) < 0);
    assert(dt_auto_transport_init(&t, &s, &usb, &bt, usb_present, NULL) < 0);
    puts("PASS automatic transport: USB priority, USB/BT/USB with same VDA, neutral loss and held Select, absent/silent input, single backend and probe error");
}
