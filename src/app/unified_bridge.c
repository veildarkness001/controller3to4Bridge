#include "dt/auto_transport.h"
#include "dt/bridge_session.h"
#include "dt/ps4_usb.h"
#include "dt/ps4_bluetooth.h"
#include "dt/log.h"
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <ps4/kernel.h>
#ifndef DT_VERSION
#define DT_VERSION "development"
#endif
#define STOP_FILE "/data/dualshock_to_ps4/stop"

static int should_stop(void *context)
{
    DtTransport *bluetooth = context;
    if (access(STOP_FILE, F_OK) == 0) return 1;
    return bluetooth->ops && dt_ps4_bluetooth_status(bluetooth) != 0;
}
static int unified_session(DtBridgeStats *stats, int *usb_power_off)
{
    DtTransport usb = {0}, bluetooth = {0}, transport = {0};
    DtAutoTransport selection = {0};
    DtVirtualPad *pad = NULL;
    int status = 1;
    *usb_power_off = 0;
    if (dt_ps4_usb_create(&usb) != 0) goto done;
    /* Native BT temporarily owns event caps; VDA must snapshot them afterward.
     * Safe unavailability (e.g. no pairing) leaves USB available. Failed
     * rollback is fatal: do not continue into USB with unknown privileges. */
    int wired = dt_ps4_usb_present(&usb);
    if (wired < 0) goto done;
    if (!wired) {
        int bt = dt_ps4_bluetooth_create(&bluetooth);
        if (bt == -2) { status = 6; goto done; }
        if (bt != 0) dt_log("Bluetooth unavailable; USB remains available for this activation\n");
    } else dt_log("USB activation; Bluetooth backend not created\n");
    if (dt_auto_transport_init(&transport, &selection, &usb,
                              bluetooth.ops ? &bluetooth : NULL, dt_ps4_usb_present,
                              dt_ps4_bluetooth_quiesce) != 0) goto done;
    const DtBridgeConfig config = {.should_stop = should_stop, .context = &bluetooth,
                                   .restart_on_input_loss = 1};
    dt_log("waiting for DS3 input over USB or paired Bluetooth; no virtual controller yet\n");
    int ready = dt_bridge_wait_for_input(&transport, &config);
    if (ready == DT_BRIDGE_INPUT_LOST) { status = ready; goto done; }
    if (ready <= 0) { status = ready < 0 ? 1 : 0; goto done; }
    /* USB may arrive while an initial Bluetooth activation is waiting. Unwind
     * that listener before opening a wired VDA; no BT registration during USB. */
    if (selection.active == &usb && bluetooth.ops) {
        status = DT_BRIDGE_INPUT_LOST; goto done;
    }
    if (dt_virtual_open(&pad) != 0) goto done;
    dt_log("ready for DS3 user selection; USB takes priority\n");
    status = dt_bridge_run(&transport, pad, &config, stats);
    *usb_power_off = status == DT_BRIDGE_POWER_OFF && selection.active == &usb;
done:
    if (bluetooth.ops && dt_ps4_bluetooth_status(&bluetooth) != 0) status = 7;
    if (pad && dt_virtual_close(pad) != 0) status = 5;
    if (bluetooth.ops && dt_ps4_bluetooth_destroy(&bluetooth) != 0) status = 6;
    /* A powered-off wired controller cannot signal a fresh PS connection.
     * Wait for unplug before arming it again instead of reopening the selector. */
    if (status == DT_BRIDGE_POWER_OFF && *usb_power_off) {
        dt_log("wired controller disabled; unplug USB to arm a new activation\n");
        int present = -1;
        while (access(STOP_FILE, F_OK) != 0 && (present = dt_ps4_usb_present(&usb)) > 0)
            usleep(100000);
        if (access(STOP_FILE, F_OK) == 0) status = 0;
        else if (present < 0) status = 1;
    }
    if (usb.ops && dt_ps4_usb_destroy(&usb) != 0) status = 10;
    return status;
}
int main(void)
{
    int lock = open("/data/dualshock_to_ps4-usb.lock", O_CREAT | O_WRONLY, 0666);
    if (lock < 0) return 1;
    if (flock(lock, LOCK_EX | LOCK_NB) != 0) { close(lock); return 1; }
    if (dt_log_open_named("unified-bridge.log") != 0) { close(lock); return 1; }
    dt_log("Unified bridge %s pid=%d firmware=0x%08x duration=unlimited manual-stop\n",
           DT_VERSION, getpid(), kernel_get_fw_version());
    (void)unlink(STOP_FILE);
    DtBridgeStats total = {0};
    int status;
    do {
        DtBridgeStats stats = {0}; int usb_power_off = 0;
        status = unified_session(&stats, &usb_power_off);
        total.valid += stats.valid; total.invalid += stats.invalid; total.sent += stats.sent;
        total.connections += stats.connections; total.read_errors += stats.read_errors;
        total.stale += stats.stale; total.elapsed_us += stats.elapsed_us;
        if (status != DT_BRIDGE_POWER_OFF && status != DT_BRIDGE_INPUT_LOST) break;
        dt_log("activation cleanup complete reason=%s; waiting for a new activation\n",
               status == DT_BRIDGE_POWER_OFF ? "menu power-off" : "transport change/loss");
        if (access(STOP_FILE, F_OK) == 0) { status = 0; break; }
    } while (1);
    dt_log("bridge finished status=%d valid=%llu invalid=%llu sent=%llu connections=%llu read_errors=%llu stale=%llu elapsed_s=%llu\n",
           status, (unsigned long long)total.valid, (unsigned long long)total.invalid,
           (unsigned long long)total.sent, (unsigned long long)total.connections,
           (unsigned long long)total.read_errors, (unsigned long long)total.stale,
           (unsigned long long)(total.elapsed_us / 1000000));
    dt_log_close(); close(lock); return status;
}
