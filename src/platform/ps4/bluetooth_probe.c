#include "dt/ps4_bluetooth_probe.h"
#include "dt/log.h"
#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ps4/kernel.h>
#ifndef DT_BT_SOCKET_FAMILY
#define DT_BT_SOCKET_FAMILY AF_BLUETOOTH
#endif

/* Name/NID pairs from idc/ps4libdoc 9.00 a71315e7f36e312ae71e9e3a92982e9ffbfc725f.
 * Names establish existence only, not signatures or behavior on firmware 13.52.
 * Resolved addresses are neither invoked nor logged. */
static const struct { const char *name, *nid; } exports[] = {
    {"sceBluetoothHidInit", "tul3-GzejQc"},
    {"sceBluetoothHidParamInitialize", "HNbQyWI-BtU"},
    {"sceBluetoothHidThreadParamInitialize", "o+5KjZ3r4O4"},
    {"sceBluetoothHidRegisterCallback", "4Ypfo9RIwfM"},
    {"sceBluetoothHidUnregisterCallback", "upEB3XUvH8o"},
    {"sceBluetoothHidRegisterDevice", "4FUZ+c52d2k"},
    {"sceBluetoothHidUnregisterDevice", "u+lIalCYOho"},
    {"sceBluetoothHidGetDeviceInfo", "YWT1GrcWuQs"},
    {"sceBluetoothHidGetInputReport", "hAgb6kz3UcU"},
    {"sceBluetoothHidSetFeatureReport", "1bG5j0mzOF0"},
    {"sceBluetoothHidGetFeatureReport", "+JQyH57+l4k"},
    {"sceBluetoothHidGetReportDescriptor", "XhoOjXnv+QQ"},
    {"sceBluetoothHidDisconnectDevice", "wZtLdyq2dKg"},
};

int dt_ps4_bluetooth_probe(void)
{
    /* stat only; opening /dev/bt_dev may have undocumented side effects. */
    const char *paths[] = {"/dev/bt_dev", "/dev/bt",
                           "/system/common/lib/libSceBluetoothHid.sprx"};
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        struct stat st;
        int ret = stat(paths[i], &st), error = ret < 0 ? errno : 0;
        dt_log("BT candidate path=%s stat=%d errno=%d\n", paths[i], ret, error);
    }
    uint32_t module = 0;
    int ret = kernel_dynlib_handle(-1, "libSceBluetoothHid.sprx", &module);
    dt_log("BT HID already-loaded lookup=%d; no module load attempted\n", ret);
    if (ret == 0) {
        for (unsigned i = 0; i < sizeof(exports) / sizeof(exports[0]); ++i) {
            intptr_t address = kernel_dynlib_resolve(-1, module, exports[i].nid);
            dt_log("BT export %s present=%d\n", exports[i].name, address > 0);
        }
    }
    /* FreeBSD SDK constants. Socket creation alone is not evidence of a usable
     * host controller or routed channels. No bind/listen/HCI command is issued. */
    const struct { int type, protocol; const char *name; } sockets[] = {
        {SOCK_SEQPACKET, 135, "FreeBSD-L2CAP"},
        {SOCK_RAW, 134, "FreeBSD-HCI"}
    };
    int status = 0;
    for (unsigned i = 0; i < sizeof(sockets) / sizeof(sockets[0]); ++i) {
        int fd = socket(DT_BT_SOCKET_FAMILY, sockets[i].type, sockets[i].protocol);
        int error = fd < 0 ? errno : 0;
        dt_log("BT socket candidate=%s created=%d errno=%d\n", sockets[i].name, fd >= 0, error);
        if (fd >= 0) {
            int closed = close(fd);
            dt_log("BT socket close=%d errno=%d\n", closed, closed < 0 ? errno : 0);
            if (closed != 0) status = 2;
        }
    }
    dt_log("BT inspection only; native ABI/pairing/channel access/DS3 support UNVERIFIED\n");
    return status;
}
