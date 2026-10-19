#include "dt/controller.h"
#include "dt/ps4_usb.h"
#include "dt/log.h"
#ifdef DT_USB_PROBE
#include "dt/ps4_usb_probe.h"
#endif
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>
#include <ps4/kernel.h>
#ifndef DT_VERSION
#define DT_VERSION "development"
#endif

int main(void)
{
    /* Lock in /data first, before truncating a previous run's log. */
    int lock = open("/data/dualshock_to_ps4-usb.lock", O_CREAT | O_WRONLY, 0666);
    if (lock < 0) return 1;
    if (flock(lock, LOCK_EX | LOCK_NB) != 0) { close(lock); return 1; }
#ifdef DT_USB_PROBE
    if (dt_log_open_named("usb-probe.log") != 0) { close(lock); return 1; }
#else
    if (dt_log_open() != 0) { close(lock); return 1; }
#endif
#ifdef DT_USB_PROBE
    dt_log("USB probe %s pid=%d firmware=0x%08x\n", DT_VERSION, getpid(), kernel_get_fw_version());
#else
    dt_log("USB diagnostic %s pid=%d firmware=0x%08x\n", DT_VERSION, getpid(), kernel_get_fw_version());
#endif
    dt_log("manual probe; no virtual pads, pairing writes, network listener or AutoRun\n");
    uint64_t start = dt_now_us(), next_log = 0;
    int status = 1;
    unsigned valid = 0, invalid = 0, timeouts = 0;
#ifdef DT_USB_PROBE_F4
    int f4_attempted = 0;
#endif
    DtTransport transport = {0};
    if (!start) { dt_log("monotonic clock unavailable\n"); goto done; }
#ifdef DT_USB_PROBE_WAIT
    dt_log("probe waiting up to 60s for DS3; input window starts after open\n");
    if (dt_ps4_usb_open(&transport, start + 60000000u) != 0) goto done;
    start = dt_now_us();
    if (!start) { transport.ops->close(transport.context); goto done; }
#else
    if (dt_ps4_usb_open(&transport, start + 10000000u) != 0) goto done;
#endif
    while (dt_now_us() && dt_now_us() - start < 60000000u) {
#ifdef DT_USB_PROBE_F4
        if (!f4_attempted && dt_now_us() - start >= 10000000u) {
            f4_attempted = 1;
            dt_log("probe F4 baseline valid=%u invalid=%u timeouts=%u\n", valid, invalid, timeouts);
            if (!valid) {
                dt_log("probe F4 intervention: same handle, one USB streaming enable\n");
                int enabled = dt_ps4_usb_probe_enable_stream(&transport);
                dt_log("probe F4 intervention status=%d\n", enabled);
            } else dt_log("probe F4 skipped: baseline already has valid input\n");
        }
#endif
        DtInputReport report = {0};
        DtControllerState state;
        int ret = transport.ops->read_report(transport.context, &report, 50);
        if (ret < 0) { status = 2; break; }
        if (!ret) { ++timeouts; continue; }
        if (dt_ds3_decode(&report, &state) != 0) {
            if (++invalid <= 5) dt_log("unrecognized report length=%u id=0x%02x\n",
                                      (unsigned)report.size, report.size ? report.data[0] : 0);
            continue;
        }
        ++valid;
        status = 0;
        /* At most ten log lines/sec: USB input need not incur disk I/O each frame. */
        if (report.timestamp_us >= next_log) {
            dt_log("input buttons=0x%05x sticks=%u,%u,%u,%u triggers=%u,%u battery=0x%02x\n",
                   state.buttons, state.lx, state.ly, state.rx, state.ry, state.l2, state.r2,
                   state.battery_raw);
            next_log = report.timestamp_us + 100000u;
        }
    }
#ifdef DT_USB_PROBE
    dt_log("probe baseline summary status=%d valid=%u invalid=%u timeouts=%u\n", status, valid, invalid, timeouts);
    dt_ps4_usb_probe_status(&transport);
#endif
    transport.ops->close(transport.context);
done:
    dt_log("finished status=%d valid=%u invalid=%u timeouts=%u\n", status, valid, invalid, timeouts);
    dt_log_close();
    close(lock);
    return status;
}
