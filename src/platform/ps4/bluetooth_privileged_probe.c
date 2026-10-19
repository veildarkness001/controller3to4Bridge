#include "dt/ps4_bluetooth_probe.h"
#include "dt/log.h"
#include <ps4/kernel.h>
#include <string.h>
#include <unistd.h>

int dt_ps4_bluetooth_privileged_probe(void)
{
    pid_t pid = getpid();
    uint64_t authid = kernel_get_ucred_authid(pid);
    uint8_t saved_caps[16], caps[16];
    if (!authid || kernel_get_ucred_caps(pid, saved_caps) != 0) {
        dt_log("BT cannot snapshot privileges; no experiment attempted\n");
        return 1;
    }
    memset(caps, 0xff, sizeof(caps));
    /* Same temporary profile as virtual_pad.c, no VDA/Pad API calls here. */
    int status = 1;
    int a = kernel_set_ucred_authid(pid, UINT64_C(0x3800000000010003));
    int c = a == 0 ? kernel_set_ucred_caps(pid, caps) : -1;
    dt_log("BT temporary privilege setters authid=%d caps=%d\n", a, c);
    if (a == 0 && c == 0) status = dt_ps4_bluetooth_native_probe();
    a = kernel_set_ucred_authid(pid, authid);
    c = kernel_set_ucred_caps(pid, saved_caps);
    dt_log("BT restore privileges authid=%d caps=%d\n", a, c);
    if (a != 0 || c != 0) status = 3;
    return status;
}
