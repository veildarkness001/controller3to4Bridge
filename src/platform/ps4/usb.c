#include "dt/ps4_usb.h"
#include "dt/log.h"
#ifdef DT_USB_PROBE
#include "dt/ps4_usb_probe.h"
#endif
#include "usbd_abi.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ps4/kernel.h>

#ifdef DT_USB_PROBE_PACKET64
#define DT_USB_READ_SIZE 64u
#else
#define DT_USB_READ_SIZE 128u
#endif

typedef struct {
    DtUsbdApi api;
    void *module;
    DtUsbHandle *handle;
    int initialized, claimed, interface;
    uint8_t endpoint, output_endpoint;
    uint64_t reads, empty, timeouts, reports, errors, bytes;
    int last_result, last_size;
    uint64_t read_us, min_us, max_us, next_read_log;
} DtUsb;

static int enable_stream(DtUsb *u)
{
    /* USB streaming enable documented by DsHidMini/PS3 USB captures.
     * This volatile F4 command is distinct from pairing (F5) and BT 42 03. */
    unsigned char command[] = {0x42, 0x0c, 0, 0};
    uint64_t start = dt_now_us();
    int ret = u->api.control(u->handle, 0x21, 0x09, 0x03f4,
                            (uint16_t)u->interface, command, sizeof(command), 250);
    dt_log("USB F4 enable result=%d hex=0x%08x duration_us=%llu\n",
           ret, (uint32_t)ret, (unsigned long long)(dt_now_us() - start));
    return ret == sizeof(command) ? 0 : -1;
}

static void *resolve(void *module, const char *name)
{
    void *address = dlsym(module, name);
    if (!address)
        address = (void *)kernel_dynlib_dlsym(-1, (uint32_t)(uintptr_t)module, name);
    dt_log("symbol %s: %s\n", name, address ? "resolved" : "MISSING");
    return address;
}

static int load_api(DtUsb *u)
{
    u->module = dlopen("/system/common/lib/libSceUsbd.sprx", RTLD_NOW | RTLD_LOCAL);
    dt_log("load libSceUsbd: %s\n", u->module ? "OK" : dlerror());
    if (!u->module) return -1;
#define LOAD(field, name) do { \
    void *address = resolve(u->module, name); \
    if (!address) return -1; \
    _Static_assert(sizeof(u->api.field) == sizeof(address), "function pointer ABI"); \
    memcpy(&u->api.field, &address, sizeof(address)); \
} while (0)
    LOAD(init, "sceUsbdInit"); LOAD(exit, "sceUsbdExit");
    LOAD(list, "sceUsbdGetDeviceList"); LOAD(free_list, "sceUsbdFreeDeviceList");
    LOAD(descriptor, "sceUsbdGetDeviceDescriptor");
    LOAD(config, "sceUsbdGetActiveConfigDescriptor");
    LOAD(free_config, "sceUsbdFreeConfigDescriptor");
    LOAD(open, "sceUsbdOpen"); LOAD(close, "sceUsbdClose");
    LOAD(claim, "sceUsbdClaimInterface"); LOAD(release, "sceUsbdReleaseInterface");
    LOAD(control, "sceUsbdControlTransfer"); LOAD(interrupt, "sceUsbdInterruptTransfer");
#undef LOAD
    int ret = u->api.init();
    dt_log("sceUsbdInit: %d (0x%08x)\n", ret, (uint32_t)ret);
    if (ret != 0) return -1;
    u->initialized = 1;
    return 0;
}

static void disconnect_device(DtUsb *u)
{
#ifdef DT_USB_PROBE
    if (u->reads)
        dt_log("probe timing reads=%llu total_us=%llu min_us=%llu max_us=%llu\n",
               (unsigned long long)u->reads, (unsigned long long)u->read_us,
               (unsigned long long)u->min_us, (unsigned long long)u->max_us);
#endif
    if (u->handle && u->reads)
        dt_log("USB read summary reads=%llu empty=%llu timeouts=%llu reports=%llu bytes=%llu errors=%llu last=0x%08x last_size=%d\n",
               (unsigned long long)u->reads, (unsigned long long)u->empty,
               (unsigned long long)u->timeouts, (unsigned long long)u->reports,
               (unsigned long long)u->bytes, (unsigned long long)u->errors,
               (uint32_t)u->last_result, u->last_size);
    if (u->claimed) dt_log("release interface: %d\n", u->api.release(u->handle, u->interface));
    if (u->handle) u->api.close(u->handle);
    u->claimed = 0; u->handle = NULL;
    u->reads = u->empty = u->timeouts = u->reports = u->errors = u->bytes = 0;
    u->last_result = u->last_size = 0;
    u->read_us = u->min_us = u->max_us = u->next_read_log = 0;
}

static void close_transport(void *context)
{
    DtUsb *u = context;
    if (!u) return;
    disconnect_device(u);
    if (u->initialized) { u->api.exit(); dt_log("sceUsbdExit called (void)\n"); }
    /* The pinned SDK implements dlclose with SYS_dynlib_unload. Pair our own
     * load only; a successful return does not prove that shared USB state was
     * restored. Do not infer POSIX loader/refcount semantics from this name. */
    if (u->module) dt_log("close library reference: %d\n", dlclose(u->module));
    free(u);
}

static int find_endpoint(DtUsb *u, const DtUsbConfig *config)
{
    if (!config || config->interface_count > 16 || !config->interfaces) return -1;
    for (unsigned i = 0; i < config->interface_count; ++i) {
        const DtUsbInterface *iface = &config->interfaces[i];
        if (!iface->alternates || iface->count < 0 || iface->count > 16) continue;
        for (int a = 0; a < iface->count; ++a) {
            const DtUsbAlternate *alt = &iface->alternates[a];
            if (alt->interface_class != 3 || alt->alternate != 0 ||
                !alt->endpoints || alt->endpoint_count > 16) continue;
            uint8_t input = 0, output = 0;
            for (unsigned e = 0; e < alt->endpoint_count; ++e) {
                const DtUsbEndpoint *ep = &alt->endpoints[e];
                if ((ep->attributes & 3) != 3) continue;
                if (!(ep->address & 0x80) && ep->max_packet >= 1) output = ep->address;
                if ((ep->address & 0x80) && (ep->attributes & 3) == 3 && ep->max_packet >= 49) {
                    input = ep->address;
                }
            }
            if (input) {
                u->interface = alt->number; u->endpoint = input; u->output_endpoint = output;
                dt_log("HID interface=%d IN=0x%02x OUT=0x%02x\n", u->interface, input, output);
#ifdef DT_USB_PROBE
                for (unsigned e = 0; e < alt->endpoint_count; ++e) {
                    const DtUsbEndpoint *ep = &alt->endpoints[e];
                    dt_log("probe endpoint address=0x%02x attributes=0x%02x max_packet=%u interval=%u\n",
                           ep->address, ep->attributes, ep->max_packet, ep->interval);
                }
#endif
                return 0;
            }
        }
    }
    return -1;
}

static int scan(DtUsb *u)
{
    DtUsbDevice **list = NULL;
    int64_t count = u->api.list(&list);
    int result = 0;
    dt_log("enumeration count=%lld\n", (long long)count);
    if (count < 0 || count > 256 || !list) {
        if (list) u->api.free_list(list, 1);
        return count < 0 || count > 256 ? -1 : 0;
    }
    for (int i = 0; i < count && !result; ++i) {
        DtUsbDescriptor desc;
        if (u->api.descriptor(list[i], &desc) != 0 ||
            desc.vendor != 0x054c || desc.product != 0x0268) continue;
        DtUsbConfig *config = NULL;
        int ret = u->api.config(list[i], &config);
        dt_log("Sony DS3 054c:0268 active config=%d\n", ret);
        if (ret != 0) { result = -1; break; }
        int found = find_endpoint(u, config);
        u->api.free_config(config);
        if (found != 0) { dt_log("no suitable HID interrupt IN endpoint\n"); result = -1; break; }
        ret = u->api.open(list[i], &u->handle);
        dt_log("open=%d handle=%s\n", ret, u->handle ? "present" : "missing");
        if (ret != 0 || !u->handle) { result = -1; break; }
        /* Do not detach native drivers or reset the USB device in this probe. */
        ret = u->api.claim(u->handle, u->interface);
        dt_log("claim=%d (0x%08x)\n", ret, (uint32_t)ret);
        if (ret != 0) { result = -1; break; }
        u->claimed = 1;
        unsigned char feature[17] = {0};
#ifdef DT_USB_PROBE
        uint64_t control_start = dt_now_us();
#endif
        /* HID GET_REPORT(feature F2): enable input without writing a BT host
         * address. Do not log feature bytes: they can contain device addresses. */
        ret = u->api.control(u->handle, 0xa1, 0x01, 0x03f2,
                             (uint16_t)u->interface, feature, sizeof(feature), 250);
        dt_log("GET_REPORT F2=%d\n", ret);
#ifdef DT_USB_PROBE
        dt_log("probe F2 duration_us=%llu\n", (unsigned long long)(dt_now_us() - control_start));
        control_start = dt_now_us();
#endif
        if (ret != 17) { result = -1; break; }
        ret = u->api.control(u->handle, 0xa1, 0x01, 0x03f5,
                             (uint16_t)u->interface, feature, 8, 250);
        dt_log("GET_REPORT F5=%d\n", ret);
#ifdef DT_USB_PROBE
        dt_log("probe F5 duration_us=%llu\n", (unsigned long long)(dt_now_us() - control_start));
#endif
        if (ret != 8) { result = -1; break; }
#ifndef DT_USB_PROBE
        /* GET_REPORT alone left original Sony pads silent in hardware tests;
         * explicitly enable streaming on every new physical handle. */
        if (enable_stream(u) != 0) { result = -1; break; }
#endif
        /* Linux hid-sony and SDL's USB initialization finish with a one-byte
         * output report from the F5 response. This is an interrupt OUT, never
         * SET_REPORT F5 (which would change pairing). No addresses are sent.
         * As in those implementations, failure of this final kick is nonfatal. */
#ifndef DT_USB_PROBE
        if (u->output_endpoint) {
            int32_t transferred = 0;
            ret = u->api.interrupt(u->handle, u->output_endpoint, feature, 1,
                                   &transferred, 250);
            dt_log("USB activation OUT result=0x%08x bytes=%d\n", (uint32_t)ret, transferred);
        } else dt_log("USB activation OUT unavailable; keeping F2/F5 initialization\n");
#else
        dt_log("probe baseline: F2/F5 only, no OUT, no reopen, IN length=%u timeout_ms=50\n", DT_USB_READ_SIZE);
#endif
        result = 1;
    }
    u->api.free_list(list, 1);
    return result;
}

static int read_report(void *context, DtInputReport *out, unsigned timeout_ms)
{
    DtUsb *u = context;
    int32_t transferred = 0;
    if (!u->handle || !timeout_ms || timeout_ms > 1000) return -1;
#ifdef DT_USB_PROBE
    uint64_t read_start = dt_now_us();
#endif
    int ret = u->api.interrupt(u->handle, u->endpoint, out->data,
                               DT_USB_READ_SIZE, &transferred, timeout_ms);
    ++u->reads; u->last_result = ret; u->last_size = transferred;
#ifdef DT_USB_PROBE
    uint64_t read_end = dt_now_us();
    uint64_t elapsed = read_end >= read_start ? read_end - read_start : 0;
    u->read_us += elapsed;
    if (u->reads == 1 || elapsed < u->min_us) u->min_us = elapsed;
    if (elapsed > u->max_us) u->max_us = elapsed;
    if (u->reads == 1 || read_end >= u->next_read_log) {
        dt_log("probe IN read=%llu result=0x%08x transferred=%d requested=%u timeout_ms=%u duration_us=%llu\n",
               (unsigned long long)u->reads, (uint32_t)ret, transferred,
               DT_USB_READ_SIZE, timeout_ms, (unsigned long long)elapsed);
        u->next_read_log = read_end + 10000000u;
    }
#endif
    if (ret == -7 || (uint32_t)ret == 0x80240007u) ++u->timeouts;
    /* Sony wraps libusb error -7 as 0x80240007. A timed-out transfer may
     * still deliver bytes; keep polling instead of ending at the first idle read. */
    if (ret != 0 && ret != -7 && (uint32_t)ret != 0x80240007u) {
        ++u->errors;
        dt_log("read error=%d (0x%08x)\n", ret, (uint32_t)ret);
        return ret < 0 ? ret : -1;
    }
    if (transferred <= 0) {
        if (!u->empty) dt_log("USB first empty read result=0x%08x bytes=%d timeout_ms=%u\n",
                              (uint32_t)ret, transferred, timeout_ms);
        ++u->empty; return 0;
    }
    if ((size_t)transferred > DT_USB_READ_SIZE) { ++u->errors; return -1; }
    if (!u->reports) dt_log("USB first frame bytes=%d report_id=0x%02x result=0x%08x\n",
                            transferred, out->data[0], (uint32_t)ret);
    ++u->reports; u->bytes += (unsigned)transferred;
    out->size = (size_t)transferred;
    out->timestamp_us = dt_now_us();
    return 1;
}

#ifdef DT_USB_PROBE
int dt_ps4_usb_probe_enable_stream(DtTransport *transport)
{
    DtUsb *u = transport ? transport->context : NULL;
    if (!u || !u->handle) return -1;
    return enable_stream(u);
}

void dt_ps4_usb_probe_status(DtTransport *transport)
{
    DtUsb *u = transport ? transport->context : NULL;
    if (!u || !u->handle) return;
    /* Run only after the baseline so these requests cannot affect its reads.
     * Unsupported requests may STALL; that alone is not evidence of failure. */
    unsigned char data[128] = {0};
    const struct { const char *name; uint8_t type, request; uint16_t value, index, size; } probes[] = {
        {"configuration", 0x80, 8, 0, 0, 1},
        {"alternate", 0x81, 10, 0, (uint16_t)u->interface, 1},
        {"endpoint-status", 0x82, 0, 0, u->endpoint, 2},
        {"input-report-1", 0xa1, 1, 0x0101, (uint16_t)u->interface, 64},
    };
    for (unsigned i = 0; i < sizeof(probes)/sizeof(probes[0]); ++i) {
        memset(data, 0, sizeof(data));
        uint64_t start = dt_now_us();
        int ret = u->api.control(u->handle, probes[i].type, probes[i].request,
                                probes[i].value, probes[i].index, data, probes[i].size, 250);
        dt_log("probe control %s result=%d hex=0x%08x duration_us=%llu\n",
               probes[i].name, ret, (uint32_t)ret, (unsigned long long)(dt_now_us() - start));
        if (i < 3 && ret == probes[i].size)
            dt_log("probe state %s value=0x%04x\n", probes[i].name,
                   (unsigned)data[0] | (ret == 2 ? (unsigned)data[1] << 8 : 0));
        if (i == 3 && ret > 0)
            dt_log("probe control input length=%d report_id=0x%02x\n", ret, data[0]);
    }
}
#endif

static int reconnect(void *context)
{
    DtUsb *u = context;
    disconnect_device(u);
    int ret = scan(u);
    if (ret <= 0) disconnect_device(u); /* Also clean partial claim/F2/F5 failure. */
    return ret;
}

static const DtTransportOps ops = { read_report, NULL, close_transport, reconnect };

int dt_ps4_usb_create(DtTransport *out)
{
    memset(out, 0, sizeof(*out));
    DtUsb *u = calloc(1, sizeof(*u));
    if (!u) return -1;
    if (load_api(u) != 0) { close_transport(u); return -1; }
    *out = (DtTransport){DT_TRANSPORT_USB, &ops, u};
    return 0;
}

int dt_ps4_usb_open(DtTransport *out, uint64_t deadline_us)
{
    if (dt_ps4_usb_create(out) != 0) return -1;
    DtUsb *u = out->context;
    while (dt_now_us() && dt_now_us() < deadline_us) {
        int found = scan(u);
        if (found < 0) { close_transport(u); memset(out, 0, sizeof(*out)); return -1; }
        if (found) { *out = (DtTransport){DT_TRANSPORT_USB, &ops, u}; return 0; }
        usleep(500000);
    }
    dt_log("DS3 not found before scan deadline\n");
    close_transport(u);
    memset(out, 0, sizeof(*out));
    return -1;
}

int dt_ps4_usb_pairing_read(DtTransport *t, uint8_t identity[6], uint8_t host[6])
{
    if (!t || t->ops!=&ops || !t->context) return -1;
    DtUsb *u=t->context;
    if (!u->handle || !u->claimed) return -1;
    uint8_t f2[17]={0}, f5[8]={0};
    int a=u->api.control(u->handle,0xa1,1,0x03f2,(uint16_t)u->interface,f2,sizeof(f2),250);
    int b=a==17 ? u->api.control(u->handle,0xa1,1,0x03f5,(uint16_t)u->interface,f5,sizeof(f5),250) : -1;
    if (a==17 && b==8) { memcpy(identity,f2+4,6); memcpy(host,f5+2,6); }
    memset(f2,0,sizeof(f2)); memset(f5,0,sizeof(f5));
    dt_log("USB pairing read F2=%d F5=%d; values omitted\n",a,b);
    return a==17 && b==8 ? 0 : -1;
}
int dt_ps4_usb_pairing_write(DtTransport *t, const uint8_t host[6])
{
    if (!t || t->ops!=&ops || !t->context) return -1;
    DtUsb *u=t->context;
    if (!u->handle || !u->claimed) return -1;
    uint8_t f5[8]={1,0}; memcpy(f5+2,host,6);
    int ret=u->api.control(u->handle,0x21,9,0x03f5,(uint16_t)u->interface,f5,sizeof(f5),250);
    memset(f5,0,sizeof(f5)); dt_log("USB pairing write F5=%d; value omitted\n",ret);
    return ret==8 ? 0 : -1;
}
