#include "dt/virtual_pad.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

enum { SAVE_FAIL=1, CAPS_FAIL, MBUS_FAIL, PAD_FAIL, PRIV_FAIL, LOG_FAIL,
       MARKER_FAIL, ADD_FAIL, SUCCESS, DELETE_FAIL };
static int scenario, adds, deletes, restored_auth, restored_caps, poll_logout;
static uint64_t now, first_stamp;
static uint8_t first_count;
static char log_queue[2048];
static int sent;

uint64_t dt_now_us(void) { return now += 1000; }
void dt_log(const char *format, ...) { (void)format; }
uint64_t kernel_get_ucred_authid(pid_t pid) { (void)pid; return 0x123456; }
int kernel_get_ucred_caps(pid_t pid, uint8_t *caps)
{ (void)pid; memset(caps, 0x12, 16); return scenario == SAVE_FAIL ? -1 : 0; }
int kernel_set_ucred_authid(pid_t pid, uint64_t authid)
{ (void)pid; if (authid == 0x123456) ++restored_auth; return 0; }
int kernel_set_ucred_caps(pid_t pid, const uint8_t *caps)
{ (void)pid; if (caps[0] == 0x12) { ++restored_caps; return 0; } return scenario == CAPS_FAIL ? -1 : 0; }
void *dt_test_dlopen(const char *name, int flags)
{ (void)name; (void)flags; return scenario == MBUS_FAIL ? NULL : (void *)1; }
char *dt_test_dlerror(void) { return "test failure"; }
int dt_test_open(const char *name, int flags, ...)
{ (void)flags; assert(!strcmp(name,"/dev/klog")); if (scenario == LOG_FAIL) { errno = EBUSY; return -1; } return 100; }
int dt_test_close(int fd) { assert(fd == 100); return 0; }
int dt_test_usleep(useconds_t delay) { now += delay; return 0; }
ssize_t dt_test_read(int fd, void *buf, size_t size)
{
    assert(fd == 100);
    size_t n = strlen(log_queue);
    if (!n) { errno = EAGAIN; return -1; }
    assert(n <= size); memcpy(buf,log_queue,n); log_queue[0] = 0; return (ssize_t)n;
}
void klog_printf(const char *format, ...)
{
    if (scenario == MARKER_FAIL) return;
    va_list args; va_start(args,format); vsnprintf(log_queue,sizeof(log_queue),format,args); va_end(args);
}
int32_t sceUserServiceInitialize(void *param) { (void)param; return 0; }
int32_t scePadInit(void) { return scenario == PAD_FAIL ? -1 : 0; }
int32_t scePadSetProcessPrivilege(int32_t priv) { assert(priv == 1); return scenario == PRIV_FAIL ? -1 : 0; }
int32_t scePadVirtualDeviceAddDevice(void *param, int32_t type)
{
    C4fVdaParam *p = param;
    assert(p->size == 32 && p->userId == 1 && type == 3);
    ++adds;
    if (scenario != ADD_FAIL)
        strcpy(log_queue,"SCE_MBUS_EVENT_DEVICE_ADDED [DeviceId:0x7030d][subType:2]\nDEVICE_OWNER_CHANGED [DeviceId:0x7030d][UserId:0x123]\n");
    return (int32_t)0x803b0006u;
}
int32_t scePadVirtualDeviceInsertData(int32_t handle, const void *data)
{
    assert(handle == 0x7030d);
    const ScePadData *p = data;
    if (!sent) { first_stamp = p->timestamp; first_count = p->count; }
    else assert(p->timestamp > first_stamp && p->count != first_count);
    ++sent;
    return 0;
}
int32_t scePadVirtualDeviceDeleteDevice(int32_t handle)
{ assert(handle == 0x7030d); ++deletes; return scenario == DELETE_FAIL ? -1 : 0; }
int32_t sceUserServiceGetLoginUserIdList(C4fUserServiceLoginList *list)
{ for (int i=0;i<4;++i) list->userId[i] = -1; if (!poll_logout) list->userId[0] = 0x123; return 0; }

int main(void)
{
    for (scenario=SAVE_FAIL;scenario<=DELETE_FAIL;++scenario) {
        adds=deletes=restored_auth=restored_caps=sent=poll_logout=0; now=1000; log_queue[0]=0;
        DtVirtualPad *p = NULL;
        int ret = dt_virtual_open(&p);
        if (scenario < SUCCESS) {
            assert(ret < 0 && !p && !deletes);
            assert(adds == (scenario == ADD_FAIL));
        } else {
            assert(ret == 0 && p && adds == 1);
            ScePadData sample; dt_ps4_neutral(&sample);
            assert(dt_virtual_send(p,&sample)==0 && dt_virtual_send(p,&sample)==0);
            assert(dt_virtual_poll(p)==0);
            poll_logout=1; now += 300000;
            assert(dt_virtual_poll(p)==1);
            int cleanup = dt_virtual_close(p);
            assert((cleanup != 0) == (scenario == DELETE_FAIL));
            assert(deletes == (scenario == DELETE_FAIL ? 3 : 1));
        }
        assert(restored_auth == (scenario != SAVE_FAIL));
        assert(restored_caps == (scenario != SAVE_FAIL));
    }
    puts("PASS VDA lifecycle: credential/init/log failures, own device only, per-pad time, logout, delete retry and restoration");
}
