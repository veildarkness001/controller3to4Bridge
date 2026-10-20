/* Real app orchestration with instrumented backend/VDA lifetimes. */
#define main dt_unified_main
#define open dt_test_open
#define close dt_test_close
#define flock dt_test_flock
#define access dt_test_access
#define unlink dt_test_unlink
#define usleep dt_test_usleep
#include "../src/app/unified_bridge.c"
#undef main
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { NORMAL, USB_ONLY, USB_CREATE_FAIL, BT_ROLLBACK_FAIL, WAIT_STOP,
       BT_FAULT, PAD_OPEN_FAIL, PAD_CLOSE_FAIL, BT_CLOSE_FAIL, BT_POWER,
       USB_POWER, USB_POWER_STOP, TRANSPORT_CHANGE, WAIT_LOSS, USB_CLOSE_FAIL,
       LOSS_PAD_CLOSE_FAIL, LOSS_BT_CLOSE_FAIL, USB_RACE, USB_PROBE_FAIL };
static int scenario, live_usb, live_bt, live_pad, runs, creates, pad_closes, destroys;
static int stop_file, unplugs, bt_creates;
static DtAutoTransport *selection;
static const DtTransportOps ops;
static void usb_close(void *context) { (void)context; assert(!live_pad && !live_bt); live_usb = 0; }
static const DtTransportOps ops = {.close = usb_close};
int dt_test_open(const char *path, int flags, ...) { (void)flags; assert(strstr(path,".lock")); return 42; }
int dt_test_close(int fd) { assert(fd == 42); return 0; }
int dt_test_flock(int fd, int flags) { (void)flags; assert(fd == 42); return 0; }
int dt_test_access(const char *path, int flags) { (void)flags; assert(!strcmp(path,STOP_FILE)); return stop_file ? 0 : -1; }
int dt_test_unlink(const char *path) { assert(!strcmp(path,STOP_FILE)); stop_file = 0; return 0; }
int dt_test_usleep(unsigned us)
{
    assert(us == 100000 && !live_bt && !live_pad && live_usb); ++unplugs;
    if (scenario == USB_POWER_STOP) stop_file = 1;
    return 0;
}
uint64_t dt_now_us(void) { return 1000000; }
uint32_t kernel_get_fw_version(void) { return 0x13520001; }
int dt_log_open_named(const char *name) { assert(!strcmp(name,"unified-bridge.log")); return 0; }
void dt_log(const char *fmt, ...) { (void)fmt; }
void dt_log_close(void) { assert(!live_pad && !live_bt && !live_usb); }
int dt_ps4_usb_create(DtTransport *t)
{
    assert(!live_usb && !live_bt && !live_pad); ++creates;
    if (scenario == USB_CREATE_FAIL) return -1;
    live_usb = 1; *t = (DtTransport){.ops = &ops, .kind = DT_TRANSPORT_USB}; return 0;
}
int dt_ps4_usb_present(DtTransport *t)
{
    assert(t->ops && live_usb);
    if (scenario == USB_PROBE_FAIL) return -1;
    return (scenario == USB_ONLY || scenario == USB_POWER || scenario == USB_POWER_STOP ||
            (scenario == USB_RACE && creates > 1)) && !unplugs;
}
int dt_ps4_usb_destroy(DtTransport *t)
{
    assert(live_usb && !live_bt && !live_pad);
    usb_close(t->context); *t = (DtTransport){0};
    return scenario == USB_CLOSE_FAIL ? -1 : 0;
}
int dt_ps4_bluetooth_create(DtTransport *t)
{
    assert(live_usb && !live_pad && !live_bt);
    ++bt_creates;
    if (scenario == BT_ROLLBACK_FAIL) return -2;
    if (scenario == USB_ONLY) return -1;
    live_bt = 1; *t = (DtTransport){.ops = &ops, .kind = DT_TRANSPORT_BLUETOOTH}; return 0;
}
int dt_ps4_bluetooth_status(const DtTransport *t)
{ assert(t->ops && live_bt); return scenario == BT_FAULT ? -1 : 0; }
int dt_ps4_bluetooth_service(DtTransport *t, unsigned timeout)
{ (void)t; (void)timeout; assert(0); return -1; }
int dt_ps4_bluetooth_quiesce(DtTransport *t) { assert(t->ops && live_bt); return 0; }
int dt_ps4_bluetooth_destroy(DtTransport *t)
{
    assert(live_bt && !live_pad); ++destroys; live_bt = 0; *t = (DtTransport){0};
    return scenario == BT_CLOSE_FAIL || scenario == LOSS_BT_CLOSE_FAIL ? -1 : 0;
}
int dt_auto_transport_init(DtTransport *t, DtAutoTransport *s, DtTransport *u,
                          DtTransport *b, int (*probe)(DtTransport *),
                          int (*quiesce)(DtTransport *))
{
    assert(live_usb && !live_pad && probe == dt_ps4_usb_present);
    assert(quiesce == dt_ps4_bluetooth_quiesce);
    assert((b != NULL) == live_bt);
    *s = (DtAutoTransport){.usb = u, .bluetooth = b, .active = b ? b : u}; selection = s;
    *t = (DtTransport){.ops = &ops, .kind = DT_TRANSPORT_AUTO}; return 0;
}
int dt_bridge_wait_for_input(DtTransport *t, const DtBridgeConfig *c)
{
    assert(t->kind == DT_TRANSPORT_AUTO && !live_pad);
    assert(c->restart_on_input_loss);
    if (scenario == WAIT_LOSS && creates == 1) return DT_BRIDGE_INPUT_LOST;
    if (scenario == WAIT_STOP) { stop_file = 1; assert(c->should_stop(c->context)); return 0; }
    if (scenario == BT_FAULT) { assert(c->should_stop(c->context)); return 0; }
    if (scenario == USB_RACE && creates == 1) selection->active = selection->usb;
    assert(!c->should_stop(c->context)); return 1;
}
int dt_virtual_open(DtVirtualPad **p)
{
    assert(!live_pad && live_usb);
    assert((selection->active == selection->usb) == !live_bt);
    if (scenario == PAD_OPEN_FAIL) return -1;
    live_pad = 1; *p = (DtVirtualPad *)1; return 0;
}
int dt_virtual_close(DtVirtualPad *p)
{ assert(p == (DtVirtualPad *)1 && live_pad); ++pad_closes; live_pad = 0; return scenario == PAD_CLOSE_FAIL || scenario == LOSS_PAD_CLOSE_FAIL ? -1 : 0; }
int dt_bridge_run(DtTransport *t, DtVirtualPad *p, const DtBridgeConfig *c, DtBridgeStats *s)
{
    assert(t->kind == DT_TRANSPORT_AUTO && p == (DtVirtualPad *)1 && live_pad);
    assert(!c->should_stop(c->context)); ++runs; s->valid = 10;
    if ((scenario == TRANSPORT_CHANGE && runs < 4) ||
        scenario == LOSS_PAD_CLOSE_FAIL || scenario == LOSS_BT_CLOSE_FAIL)
        return DT_BRIDGE_INPUT_LOST;
    if (scenario == USB_POWER || scenario == USB_POWER_STOP || scenario == BT_POWER) {
        selection->active = scenario == BT_POWER ? selection->bluetooth : selection->usb;
        if (runs == 1) return DT_BRIDGE_POWER_OFF;
    }
    return 0;
}
int main(void)
{
    for (scenario = NORMAL; scenario <= USB_PROBE_FAIL; ++scenario) {
        live_usb = live_bt = live_pad = runs = creates = pad_closes = destroys = 0;
        stop_file = unplugs = bt_creates = 0;
        int result = dt_unified_main();
        assert(!live_usb && !live_bt && !live_pad);
        if (scenario == USB_CREATE_FAIL || scenario == PAD_OPEN_FAIL || scenario == USB_PROBE_FAIL) assert(result == 1 && !runs);
        else if (scenario == BT_ROLLBACK_FAIL || scenario == BT_CLOSE_FAIL || scenario == LOSS_BT_CLOSE_FAIL) assert(result == 6 && creates == 1);
        else if (scenario == BT_FAULT) assert(result == 7 && !runs);
        else if (scenario == PAD_CLOSE_FAIL || scenario == LOSS_PAD_CLOSE_FAIL) assert(result == 5 && creates == 1);
        else if (scenario == USB_CLOSE_FAIL) assert(result == 10 && creates == 1);
        else assert(result == 0);
        if (scenario == USB_ONLY) assert(runs == 1 && !destroys && !bt_creates);
        if (scenario == BT_POWER || scenario == USB_POWER) assert(creates == 2 && runs == 2 && pad_closes == 2);
        if (scenario == USB_POWER_STOP) assert(creates == 1 && runs == 1 && unplugs == 1);
        if (scenario == NORMAL) assert(runs == 1 && pad_closes == 1 && destroys == 1);
        if (scenario == TRANSPORT_CHANGE) assert(creates == 4 && pad_closes == 4 && destroys == 4);
        if (scenario == WAIT_LOSS) assert(creates == 2 && runs == 1 && pad_closes == 1 && destroys == 2);
        if (scenario == USB_RACE) assert(creates == 2 && runs == 1 && pad_closes == 1 && bt_creates == 1 && destroys == 1);
        if (scenario == USB_PROBE_FAIL) assert(!bt_creates && !pad_closes);
    }
    puts("PASS unified app: deferred activation, optional BT, fatal rollback, VDA-before-BT cleanup, idle event servicing, menu reactivation and wired unplug rearm");
}
