#include "dt/bridge_session.h"
#include "dt/ps4_usb.h"
#include "dt/virtual_pad.h"
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
    (void)context;
    return access(STOP_FILE, F_OK) == 0;
}

int main(void)
{
    int lock = open("/data/dualshock_to_ps4-usb.lock", O_CREAT | O_WRONLY, 0666);
    if (lock < 0) return 1;
    if (flock(lock, LOCK_EX | LOCK_NB) != 0) { close(lock); return 1; }
    if (dt_log_open_named("usb-bridge.log") != 0) { close(lock); return 1; }
    dt_log("USB bridge %s pid=%d firmware=0x%08x duration=unlimited manual-stop\n",
           DT_VERSION, getpid(), kernel_get_fw_version());
    (void)unlink(STOP_FILE);
    DtTransport transport = {0};
    DtVirtualPad *pad = NULL;
    DtBridgeStats stats = {0};
    int status = 1;
    if (dt_ps4_usb_create(&transport) != 0) goto done;
    if (dt_virtual_open(&pad) != 0) goto done;
    dt_log("ready for physical input; use PS/X on DS3 to choose user\n");
    const DtBridgeConfig config = {.should_stop = should_stop};
    status = dt_bridge_run(&transport, pad, &config, &stats);
done:
    if (pad && dt_virtual_close(pad) != 0) status = 5;
    if (transport.ops) transport.ops->close(transport.context);
    dt_log("bridge finished status=%d valid=%llu invalid=%llu sent=%llu connections=%llu read_errors=%llu stale=%llu elapsed_s=%llu\n",
           status, (unsigned long long)stats.valid, (unsigned long long)stats.invalid,
           (unsigned long long)stats.sent, (unsigned long long)stats.connections,
           (unsigned long long)stats.read_errors, (unsigned long long)stats.stale,
           (unsigned long long)(stats.elapsed_us / 1000000));
    dt_log_close(); close(lock);
    return status;
}
