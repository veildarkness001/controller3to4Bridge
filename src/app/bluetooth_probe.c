#include "dt/ps4_bluetooth_probe.h"
#include "dt/log.h"
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <ps4/kernel.h>
#ifndef DT_VERSION
#define DT_VERSION "development"
#endif

int main(void)
{
    /* Share the USB lock: do not overlap even read-only reconnaissance with a
     * running bridge or replace its logs. Failure leaves existing logs intact. */
    int lock = open("/data/dualshock_to_ps4-usb.lock", O_CREAT | O_WRONLY, 0666);
    if (lock < 0) return 1;
    if (flock(lock, LOCK_EX | LOCK_NB) != 0) { close(lock); return 1; }
    if (dt_log_open_named("bluetooth-probe.log") != 0) { close(lock); return 1; }
    dt_log("Bluetooth probe %s pid=%d firmware=0x%08x\n", DT_VERSION, getpid(), kernel_get_fw_version());
#ifdef DT_BT_CACHE_PROBE
    dt_log("manual read-only known-DS3 cache metadata; no registration or pairing writes\n");
    int status = dt_ps4_bluetooth_cache_probe();
#elif defined(DT_BT_PAIRING_RESTORE)
    dt_log("manual USB original-host restoration from private backup\n");
    int status = dt_ps4_bluetooth_pairing_restore();
#elif defined(DT_BT_PAIRING_PROBE)
    dt_log("manual USB pairing with verified private original-host backup\n");
    int status = dt_ps4_bluetooth_pairing_probe();
#elif defined(DT_BT_HOST_PROBE)
    dt_log("manual read-only host property query; no pairing or registration writes\n");
    int status = dt_ps4_bluetooth_host_probe();
#elif defined(DT_BT_CODE_PROBE)
    dt_log("manual firmware-specific RX code inspection; no BT registration, pairing or kernel writes\n");
    int status = dt_ps4_bluetooth_code_probe();
#elif defined(DT_BT_NATIVE_PROBE)
    dt_log("manual native DS3 registration experiment; no pairing, VDA, module loading or AutoRun\n");
#ifdef DT_BT_EVENT_PRIVILEGES
    int status = dt_ps4_bluetooth_events_probe();
#elif defined(DT_BT_PRIVILEGED_PROBE)
    int status = dt_ps4_bluetooth_privileged_probe();
#else
    int status = dt_ps4_bluetooth_native_probe();
#endif
#else
    dt_log("manual inspection; no VDA, HID initialization, pairing, module loading or AutoRun\n");
    int status = dt_ps4_bluetooth_probe();
#endif
#if defined(DT_BT_PAIRING_PROBE) || defined(DT_BT_PAIRING_RESTORE)
    dt_log("finished status=%d pairing_experiment=1\n", status);
#elif defined(DT_BT_NATIVE_PROBE)
    dt_log("finished status=%d native_registration=1\n", status);
#else
    dt_log("finished status=%d inspection_only=1\n", status);
#endif
    dt_log_close(); close(lock);
    return status;
}
