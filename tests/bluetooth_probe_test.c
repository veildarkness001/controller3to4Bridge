#include "dt/ps4_bluetooth_probe.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <ps4/kernel.h>

static int loaded, socket_ok, close_fail, stats, resolves, sockets, closes;
static char logs[8192];
void dt_log(const char *format, ...)
{
    va_list args; va_start(args, format);
    size_t offset = strlen(logs);
    vsnprintf(logs+offset, sizeof(logs)-offset, format, args);
    va_end(args);
}
int dt_test_stat(const char *path, struct stat *out)
{
    assert(!strcmp(path,"/dev/bt_dev") || !strcmp(path,"/dev/bt") ||
           !strcmp(path,"/system/common/lib/libSceBluetoothHid.sprx"));
    ++stats; (void)out; errno = ENOENT; return -1;
}
int kernel_dynlib_handle(pid_t pid, const char *name, uint32_t *out)
{
    assert(pid == -1 && !strcmp(name,"libSceBluetoothHid.sprx"));
    *out = 21; return loaded ? 0 : -1;
}
intptr_t kernel_dynlib_resolve(pid_t pid, uint32_t handle, const char *nid)
{
    assert(pid == -1 && handle == 21 && nid && strlen(nid) == 11);
    ++resolves;
    /* Invalid callable addresses: the probe must never call resolved exports. */
    return resolves % 2 ? 123456789 : 0;
}
int dt_test_socket(int domain, int type, int protocol)
{
    assert(domain == 36);
    assert((type == SOCK_SEQPACKET && protocol == 135) || (type == SOCK_RAW && protocol == 134));
    ++sockets; errno = EPROTONOSUPPORT; return socket_ok ? sockets+4 : -1;
}
int dt_test_close(int fd)
{
    assert(fd == 5 || fd == 6); ++closes;
    errno = EIO; return close_fail ? -1 : 0;
}
int main(void)
{
    for (loaded=0; loaded<=1; ++loaded)
        for (socket_ok=0; socket_ok<=1; ++socket_ok)
            for (close_fail=0; close_fail<=1; ++close_fail) {
                stats=resolves=sockets=closes=0; logs[0]=0;
                assert(dt_ps4_bluetooth_probe() == (socket_ok && close_fail ? 2 : 0));
                assert(stats == 3 && sockets == 2 && closes == (socket_ok ? 2 : 0));
                assert(resolves == (loaded ? 13 : 0));
                assert(strstr(logs,"UNVERIFIED") && !strstr(logs,"123456789"));
                assert(strstr(logs,"no module load attempted"));
            }
    puts("PASS Bluetooth probe: unavailable/loaded modules, no export invocation, socket failures, cleanup/error reporting, no address logs");
}
