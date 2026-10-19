/* Adapted from Control4Free 1.2.1's main.c/vda.c/web.c, GPLv3.
 * VDA research derives from seregonwar/SplashDown (GPL-3.0).
 * See vendor/control4free/README.md. Local input, one pad, no network. */
#include "dt/virtual_pad.h"
#include "dt/vda_events.h"
#include "dt/log.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <ps4/kernel.h>
#include <ps4/klog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct DtVirtualPad {
    uint64_t saved_authid, timestamp;
    uint8_t saved_caps[16], sequence;
    int saved, owned, klog_fd, accept_add, login_seen, assigned_logged;
    int32_t handle;
    pid_t pid;
    DtVdaEvents events;
    char line[2048];
    size_t used;
    uint64_t users_at;
};

static int restore(DtVirtualPad *p)
{
    if (!p->saved) return 0;
    int a = kernel_set_ucred_authid(p->pid, p->saved_authid);
    int c = kernel_set_ucred_caps(p->pid, p->saved_caps);
    dt_log("restore host privileges: authid=%d caps=%d\n", a, c);
    return a || c ? -1 : 0;
}

static int read_events(DtVirtualPad *p)
{
    if (p->klog_fd < 0) return 0;
    int total = 0;
    for (int batch = 0; batch < 8; ++batch) {
        char buf[2048];
        ssize_t n = read(p->klog_fd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return total;
        if (n <= 0) return -1;
        total += (int)n;
        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                p->line[p->used] = 0;
                dt_vda_event_line(&p->events, p->line, p->accept_add);
                p->used = 0;
            } else if (buf[i] != '\r' && p->used < sizeof(p->line) - 1)
                p->line[p->used++] = buf[i];
        }
    }
    return total;
}

int dt_virtual_send(DtVirtualPad *p, const ScePadData *sample)
{
    if (!p || !p->owned) return -1;
    ScePadData stamped = *sample;
    uint64_t now = dt_now_us();
    if (!now) return -1;
    p->timestamp = now > p->timestamp ? now : p->timestamp + 1;
    stamped.timestamp = p->timestamp;
    stamped.count = ++p->sequence;
    return scePadVirtualDeviceInsertData(p->handle, &stamped);
}

int dt_virtual_close(DtVirtualPad *p)
{
    if (!p) return 0;
    int failed = 0;
    if (p->owned) {
        ScePadData neutral;
        dt_ps4_neutral(&neutral);
        int ret = dt_virtual_send(p, &neutral);
        dt_log("final neutral sample: 0x%08x\n", (uint32_t)ret);
        for (int retry = 0; retry < 3; ++retry) {
            ret = scePadVirtualDeviceDeleteDevice(p->handle);
            dt_log("DeleteDevice attempt=%d: 0x%08x\n", retry + 1, (uint32_t)ret);
            if (ret == 0) { p->owned = 0; break; }
            usleep(20000);
        }
        if (p->owned) {
            dt_log("WARNING own virtual device not deleted: handle=0x%08x; do not create another before recovery\n", (uint32_t)p->handle);
            failed = 1;
        }
    }
    if (p->klog_fd >= 0) close(p->klog_fd);
    if (restore(p) != 0) failed = 1;
    /* libSceMbus is imported by libScePad in the host process; do not unload it. */
    free(p);
    return failed ? -1 : 0;
}

int dt_virtual_open(DtVirtualPad **out)
{
    *out = NULL;
    DtVirtualPad *p = calloc(1, sizeof(*p));
    if (!p) return -1;
    p->klog_fd = -1; p->handle = -1; p->pid = getpid();
    p->saved_authid = kernel_get_ucred_authid(p->pid);
    if (!p->saved_authid || kernel_get_ucred_caps(p->pid, p->saved_caps) != 0) {
        dt_log("cannot save host privileges; aborting VDA\n");
        goto failed;
    }
    p->saved = 1;
    uint8_t caps[16]; memset(caps, 0xff, sizeof(caps));
    if (kernel_set_ucred_authid(p->pid, UINT64_C(0x3800000000010003)) != 0 ||
        kernel_set_ucred_caps(p->pid, caps) != 0) {
        dt_log("temporary VDA privileges failed\n"); goto failed;
    }
    if (!dlopen(C4F_LIBSCEMBUS_PATH, RTLD_NOW | RTLD_GLOBAL)) {
        dt_log("libSceMbus load failed: %s\n", dlerror()); goto failed;
    }
    int ret = sceUserServiceInitialize(NULL);
    dt_log("UserServiceInitialize=0x%08x\n", (uint32_t)ret);
    ret = scePadInit();
    dt_log("scePadInit=0x%08x\n", (uint32_t)ret);
    if (ret != 0) goto failed;
    ret = scePadSetProcessPrivilege(1);
    dt_log("scePadSetProcessPrivilege=0x%08x\n", (uint32_t)ret);
    if (ret != 0) goto failed;
    p->klog_fd = open("/dev/klog", O_RDONLY | O_NONBLOCK);
    if (p->klog_fd < 0) { dt_log("cannot acquire klog reader errno=%d\n", errno); goto failed; }
    uint64_t now = dt_now_us(), until = now + 2000000, quiet = now;
    if (!now) goto failed;
    /* Drain stale device events before issuing our marker and AddDevice. */
    while (dt_now_us() < until && dt_now_us() - quiet < 150000) {
        int n = read_events(p);
        if (n < 0) goto failed;
        if (n) quiet = dt_now_us();
        usleep(5000);
    }
    memset(&p->events, 0, sizeof(p->events)); p->used = 0;
    snprintf(p->events.marker, sizeof(p->events.marker), "dt-vda-marker-%llu", (unsigned long long)dt_now_us());
    klog_printf("%s\n", p->events.marker);
    until = dt_now_us() + 2000000;
    while (!p->events.marker_seen && dt_now_us() < until) {
        if (read_events(p) < 0) goto failed;
        usleep(5000);
    }
    if (!p->events.marker_seen) { dt_log("klog reader did not deliver marker; no device created\n"); goto failed; }
    C4fVdaParam param = {.size = sizeof(param), .userId = C4F_VDA_USER_SELECT};
    for (unsigned i = 0; i < 6; ++i) param.pad[i] = (int32_t)0xdeadbeef;
    p->accept_add = 1;
    ret = scePadVirtualDeviceAddDevice(&param, C4F_VIRTUAL_DEVICE_TYPE);
    dt_log("AddDevice=0x%08x; waiting for device event\n", (uint32_t)ret);
    until = dt_now_us() + 2500000;
    while (!p->events.device_id && dt_now_us() < until) {
        if (read_events(p) < 0) break;
        usleep(5000);
    }
    if (!p->events.device_id) {
        dt_log("WARNING AddDevice issued but no identifiable device event; do not retry before checking console\n");
        goto failed;
    }
    p->handle = (int32_t)(p->events.device_id & 0xffffffffu);
    p->owned = 1; p->accept_add = 0;
    dt_log("virtual device ready id=0x%llx handle=0x%08x\n",
           (unsigned long long)p->events.device_id, (uint32_t)p->handle);
    *out = p;
    return 0;
failed:
    dt_virtual_close(p);
    return -1;
}

int dt_virtual_poll(DtVirtualPad *p)
{
    if (read_events(p) < 0) { dt_log("VDA log reader lost\n"); return -1; }
    if (p->events.assigned && !p->assigned_logged) {
        dt_log("native user assignment confirmed\n");
        p->assigned_logged = 1;
        close(p->klog_fd); p->klog_fd = -1;
        dt_log("klog reader released after assignment\n");
    }
    uint64_t now = dt_now_us();
    if (!p->events.assigned || now < p->users_at) return 0;
    p->users_at = now + 250000;
    C4fUserServiceLoginList users = {{-1, -1, -1, -1}};
    if (sceUserServiceGetLoginUserIdList(&users) != 0) return 0;
    int present = 0;
    for (unsigned i = 0; i < 4; ++i)
        if ((uint32_t)users.userId[i] == p->events.user_id) present = 1;
    if (present) p->login_seen = 1;
    else if (p->login_seen) { dt_log("assigned user logged out\n"); return 1; }
    return 0;
}
