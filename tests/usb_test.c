#include "dt/ps4_usb.h"
#ifdef DT_USB_PROBE
#include "dt/ps4_usb_probe.h"
#endif
#include "../src/platform/ps4/usbd_abi.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

enum { LOAD_FAIL = 1, SYMBOL_FAIL, INIT_FAIL, CONFIG_FAIL, NO_ENDPOINT,
       OPEN_FAIL, CLAIM_FAIL, F2_SHORT, F5_SHORT, F4_SHORT, SUCCESS };
static int scenario, exits, closes, releases, freed_lists, freed_configs, unloaded;
static int transfer_result, transfer_size, present = 1, kick_result, kicks;
static uint64_t now;
static int f4_result = 4, f4_calls;
#ifdef DT_USB_PROBE
static int probe_controls, probe_fail;
#endif
static DtUsbEndpoint endpoints[2] = {{.address = 0x81, .attributes = 3, .max_packet = 64},
                                      {.address = 0x02, .attributes = 3, .max_packet = 64}};
static DtUsbAlternate alt = {.number = 0, .interface_class = 3, .endpoint_count = 2, .endpoints = endpoints};
static DtUsbInterface iface = {.alternates = &alt, .count = 1};
static DtUsbConfig config = {.interface_count = 1, .interfaces = &iface};
static DtUsbDevice *devices[] = {(DtUsbDevice *)0x111, NULL};

void dt_log(const char *format, ...) { (void)format; }
uint64_t dt_now_us(void) { return ++now; }
void *dt_test_dlopen(const char *path, int flags)
{ (void)path; (void)flags; return scenario == LOAD_FAIL ? NULL : (void *)0x123; }
char *dt_test_dlerror(void) { return "simulated load failure"; }
int dt_test_dlclose(void *module) { assert(module == (void *)0x123); ++unloaded; return 0; }
intptr_t kernel_dynlib_dlsym(pid_t pid, uint32_t module, const char *symbol)
{ (void)pid; (void)module; (void)symbol; return 0; }
static int32_t init(void) { return scenario == INIT_FAIL ? -1 : 0; }
static void finish(void) { ++exits; }
static int64_t list(DtUsbDevice ***out) { *out = devices; return present; }
static void free_list(DtUsbDevice **p, int32_t unref) { assert(p == devices && unref == 1); ++freed_lists; }
static int32_t descriptor(DtUsbDevice *d, DtUsbDescriptor *out)
{ (void)d; memset(out, 0, sizeof(*out)); out->vendor = 0x054c; out->product = 0x0268; return 0; }
static int32_t get_config(DtUsbDevice *d, DtUsbConfig **out)
{ (void)d; if (scenario == CONFIG_FAIL) return -1; *out = &config; return 0; }
static void free_config(DtUsbConfig *p) { assert(p == &config); ++freed_configs; }
static int32_t open_device(DtUsbDevice *d, DtUsbHandle **out)
{ (void)d; if (scenario == OPEN_FAIL) return -1; *out = (DtUsbHandle *)0x222; return 0; }
static void close_device(DtUsbHandle *h) { assert(h == (DtUsbHandle *)0x222); ++closes; }
static int32_t claim(DtUsbHandle *h, int32_t i)
{ (void)h; assert(i == 0); return scenario == CLAIM_FAIL ? -6 : 0; }
static int32_t release(DtUsbHandle *h, int32_t i)
{ (void)h; assert(i == 0); ++releases; return 0; }
static int32_t control(DtUsbHandle *h, uint8_t type, uint8_t req, uint16_t value,
                       uint16_t index, unsigned char *data, uint16_t size, uint32_t timeout)
{
    if (value == 0x03f5 && type == 0x21) {
        const uint8_t expected[8]={1,0,1,2,3,4,5,6};
        assert(req==9 && index==0 && size==8 && timeout==250);
        assert(!memcmp(data,expected,8));
        return scenario==F5_SHORT ? 7 : 8;
    }
    if (value == 0x03f4) {
        const unsigned char command[] = {0x42, 0x0c, 0, 0};
        assert(type == 0x21 && req == 9 && index == 0 && size == 4 && timeout == 250);
        assert(!memcmp(data, command, sizeof(command)));
        ++f4_calls;
        return scenario == F4_SHORT ? 3 : f4_result;
    }
#ifdef DT_USB_PROBE
    if (value != 0x03f2 && value != 0x03f5) {
        assert(timeout == 250);
        assert((type == 0x80 && req == 8 && size == 1 && index == 0) ||
               (type == 0x81 && req == 10 && size == 1 && index == 0) ||
               (type == 0x82 && req == 0 && size == 2 && index == 0x81) ||
               (type == 0xa1 && req == 1 && value == 0x0101 && size == 64 && index == 0));
        ++probe_controls;
        if (probe_fail) return (int32_t)0x80240009u;
        memset(data, 0, size); data[0] = 1;
        return size;
    }
#endif
    (void)h; memset(data, 0xaa, size); data[0] = 1;
    assert(type == 0xa1 && req == 1 && index == 0 && timeout > 0);
    assert((value == 0x03f2 && size == 17) || (value == 0x03f5 && size == 8));
    if ((value == 0x03f2 && scenario == F2_SHORT) || (value == 0x03f5 && scenario == F5_SHORT)) return size - 1;
    return size;
}
static int32_t interrupt(DtUsbHandle *h, unsigned char ep, unsigned char *data,
                         int32_t size, int32_t *transferred, uint32_t timeout)
{
    (void)h;
    if (ep == 0x02) {
        assert(size == 1 && data[0] == 1 && timeout == 250);
        ++kicks; *transferred = kick_result ? 0 : 1; return kick_result;
    }
    assert(ep == 0x81 && timeout > 0 && timeout <= 1000);
#ifdef DT_USB_PROBE_PACKET64
    assert(size == 64);
#else
    assert(size == 128);
#endif
    memset(data, 0, (size_t)size); data[0] = 1;
    *transferred = transfer_size; return transfer_result;
}
void *dt_test_dlsym(void *module, const char *name)
{
    assert(module == (void *)0x123);
    if (scenario == SYMBOL_FAIL) return NULL;
#define SYMBOL(text, func) if (!strcmp(name, text)) { \
    __typeof__(&func) ptr = &func; void *out; \
    _Static_assert(sizeof(ptr) == sizeof(out), "function pointer ABI"); \
    memcpy(&out, &ptr, sizeof(out)); return out; }
    SYMBOL("sceUsbdInit", init); SYMBOL("sceUsbdExit", finish);
    SYMBOL("sceUsbdGetDeviceList", list); SYMBOL("sceUsbdFreeDeviceList", free_list);
    SYMBOL("sceUsbdGetDeviceDescriptor", descriptor); SYMBOL("sceUsbdGetActiveConfigDescriptor", get_config);
    SYMBOL("sceUsbdFreeConfigDescriptor", free_config); SYMBOL("sceUsbdOpen", open_device);
    SYMBOL("sceUsbdClose", close_device); SYMBOL("sceUsbdClaimInterface", claim);
    SYMBOL("sceUsbdReleaseInterface", release); SYMBOL("sceUsbdControlTransfer", control);
    SYMBOL("sceUsbdInterruptTransfer", interrupt);
#undef SYMBOL
    assert(!"unexpected symbol"); return NULL;
}

int main(void)
{
    for (scenario = LOAD_FAIL; scenario <= SUCCESS; ++scenario) {
#ifdef DT_USB_PROBE
        if (scenario == F4_SHORT) continue; /* Probe never sends F4 during init. */
#endif
        DtTransport t = {0};
        exits = closes = releases = freed_lists = freed_configs = unloaded = 0;
        now = 1; kicks = 0;
        f4_calls = 0;
        endpoints[0].attributes = scenario == NO_ENDPOINT ? 2 : 3;
        int ret = dt_ps4_usb_open(&t, 1000000);
        int expected_success = scenario == SUCCESS;
        if (!expected_success) assert(ret != 0 && t.ops == NULL);
        else {
            assert(ret == 0 && t.kind == DT_TRANSPORT_USB && !t.ops->set_feedback);
#ifdef DT_USB_PROBE
            assert(kicks == 0);
            assert(probe_controls == 0); /* No extra control request during baseline. */
            dt_ps4_usb_probe_status(&t);
            assert(probe_controls == 4);
            probe_fail = 1;
            dt_ps4_usb_probe_status(&t);
            assert(probe_controls == 8); /* Optional STALLs never reopen/reset. */
            probe_fail = 0;
            assert(f4_calls == 0); /* Never sent implicitly during init/status. */
            assert(dt_ps4_usb_probe_enable_stream(&t) == 0);
            f4_result = -7;
            assert(dt_ps4_usb_probe_enable_stream(&t) < 0);
            f4_result = 3;
            assert(dt_ps4_usb_probe_enable_stream(&t) < 0);
            f4_result = 4;
            assert(f4_calls == 3 && kicks == 0 && closes == 0);
#else
            assert(kicks == 1);
            assert(f4_calls == 1);
#endif
            DtInputReport report;
            transfer_result = -7; transfer_size = 0;
            assert(t.ops->read_report(t.context, &report, 50) == 0);
            transfer_result = (int32_t)0x80240007u;
            assert(t.ops->read_report(t.context, &report, 50) == 0);
            transfer_size = 20;
            assert(t.ops->read_report(t.context, &report, 50) == 1 && report.size == 20);
            transfer_result = (int32_t)0x80240004u; transfer_size = 0;
            assert(t.ops->read_report(t.context, &report, 50) < 0);
            transfer_result = 0; transfer_size = 49;
            assert(t.ops->read_report(t.context, &report, 50) == 1 && report.size == 49);
            transfer_result = -7; transfer_size = 20;
            assert(t.ops->read_report(t.context, &report, 50) == 1 && report.size == 20);
            transfer_size = 129;
            assert(t.ops->read_report(t.context, &report, 50) < 0);
#ifdef DT_USB_PROBE_PACKET64
            transfer_size = 65;
            assert(t.ops->read_report(t.context, &report, 50) < 0);
#endif
            transfer_result = -4; transfer_size = 0;
            assert(t.ops->read_report(t.context, &report, 50) == -4);
            assert(t.ops->read_report(t.context, &report, 0) < 0);
            /* Repeated physical recovery retains the initialized library;
             * every old/partial handle is released exactly once. */
            for (int cycle = 0; cycle < 100; ++cycle) {
                assert(t.ops->reconnect(t.context) == 1);
                assert(exits == 0 && unloaded == 0);
            }
#ifndef DT_USB_PROBE
            assert(f4_calls == 101); /* Every reopened handle re-enables input. */
#endif
            t.ops->close(t.context);
        }
        assert(exits == (scenario > INIT_FAIL));
        int cycles = expected_success ? 100 : 0;
        assert(closes == (scenario >= CLAIM_FAIL) + cycles);
        assert(releases == (scenario >= F2_SHORT) + cycles);
        assert(freed_lists == (scenario > INIT_FAIL) + cycles);
        assert(freed_configs == (scenario > CONFIG_FAIL) + cycles);
        assert(unloaded == (scenario != LOAD_FAIL));
    }
    DtTransport t;
    scenario = SUCCESS;
    assert(dt_ps4_usb_create(&t) == 0);
    assert(t.ops->reconnect(t.context) == 1);
    uint8_t identity[6]={0}, host[6]={0};
    const uint8_t new_host[6]={1,2,3,4,5,6};
    assert(dt_ps4_usb_pairing_read(&t,identity,host)==0);
    for(unsigned i=0;i<6;++i) assert(identity[i]==0xaa && host[i]==0xaa);
    assert(dt_ps4_usb_pairing_write(&t,new_host)==0);
    scenario=F5_SHORT;
    memset(identity,0,6); memset(host,0,6);
    assert(dt_ps4_usb_pairing_read(&t,identity,host)<0 && identity[0]==0 && host[0]==0);
    assert(dt_ps4_usb_pairing_write(&t,new_host)<0);
    scenario=SUCCESS;
    present = 0;
    int before = closes;
    assert(t.ops->reconnect(t.context) == 0);
    assert(closes == before + 1);
    DtInputReport report;
    assert(dt_ps4_usb_pairing_read(&t,identity,host)<0);
    assert(dt_ps4_usb_pairing_write(&t,new_host)<0);
    assert(t.ops->read_report(t.context, &report, 4) < 0);
    present = 1;
    assert(t.ops->reconnect(t.context) == 1);
    scenario = F5_SHORT;
    before = closes;
    assert(t.ops->reconnect(t.context) < 0);
    assert(closes == before + 2); /* Previous handle and failed attempt. */
    scenario = SUCCESS;
    kick_result = -7; /* Like Linux/SDL, the optional kick may fail. */
    assert(t.ops->reconnect(t.context) == 1);
    t.ops->close(t.context);
    alt.endpoint_count = 1;
    assert(dt_ps4_usb_create(&t) == 0);
    assert(t.ops->reconnect(t.context) == 1); /* IN-only fallback. */
    t.ops->close(t.context);
    puts("PASS USB lifecycle: load/symbol/init/config/endpoint/open/claim/feature failures, cleanup and reads");
}
