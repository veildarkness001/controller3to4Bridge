#include "dt/ps4_bluetooth_probe.h"
#include "../src/platform/ps4/bluetooth_native_abi.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/event.h>
#include <time.h>
#include <unistd.h>

enum { OK, OPEN_FAIL, WRITE_SHORT, REGISTER_FAIL, UNREGISTER_FAIL, READ_FAIL,
       BAD_EVENT, CLOCK_FAIL, CLOSE_FAIL, KQUEUE_FAIL, ADD_FAIL, WAIT_FAIL, WAIT_TIMEOUT, READY_ERROR, READY_WRONG_FD };
static int scenario, closes, writes, registrations, unregistrations, reads;
static uint64_t now;
static int queue_closes, waits, cache_checks;
static int cleanup_mode, disconnects;
int dt_ps4_bluetooth_link_present(uint64_t peer) {
 assert(peer==UINT64_C(0x112233445566));
 if(cleanup_mode==3)return -1;
 if(cleanup_mode==2)return 1;
 return cleanup_mode && !disconnects;
}
int dt_ps4_bluetooth_known_peer(uint64_t *out) { *out=UINT64_C(0x112233445566); return 0; }
int dt_ps4_bluetooth_cache_probe(void) { ++cache_checks; return 0; }
int dt_ps4_bluetooth_link_probe(uint64_t peer) { assert(peer==UINT64_C(0x112233445566)); return 0; }
static char logs[8192];
void dt_log(const char *format, ...)
{
    va_list args; va_start(args,format);
    size_t n = strlen(logs);
    vsnprintf(logs+n,sizeof(logs)-n,format,args); va_end(args);
}
uint64_t dt_now_us(void) { return scenario == CLOCK_FAIL ? 0 : now; }
int dt_test_usleep(unsigned us) { assert(us == 10000 || us==20000); now += us; return 0; }
int dt_test_open(const char *path, int flags, ...)
{
    assert(!strcmp(path,"/dev/bt") && flags == (O_RDWR | O_NONBLOCK));
    errno=EACCES; return scenario == OPEN_FAIL ? -1 : 17;
}
ssize_t dt_test_write(int fd, const void *data, size_t size)
{
    uint64_t value; assert(fd==17 && size==8); memcpy(&value,data,size);
#ifdef DT_BT_ALL_EVENTS
    assert(value == UINT64_C(0x1ffffffff));
#else
    assert(value == DT_BT_HID_EVENT_MASK);
#endif
    ++writes;
    return scenario == WRITE_SHORT ? 7 : 8;
}
int dt_test_ioctl(int fd, unsigned long command, ...)
{
    assert(fd==17);
    va_list args; va_start(args,command);
    if(command==DT_BT_DISCONNECT) {
        uint64_t *peer=va_arg(args,uint64_t*);va_end(args);
        assert(*peer==UINT64_C(0x112233445566));++disconnects;
        return cleanup_mode==4 ? -1 : 0;
    }
    DtBtNativeRegistration *r=va_arg(args,DtBtNativeRegistration *); va_end(args);
    assert(r->vendor==0x054c && r->product==0x0268);
    if (command==DT_BT_REGISTER_DS3) { ++registrations; return scenario==REGISTER_FAIL ? -1 : 0; }
    assert(command==DT_BT_UNREGISTER_DS3); ++unregistrations;
    return scenario==UNREGISTER_FAIL ? -1 : 0;
}
ssize_t dt_test_read(int fd, void *data, size_t size)
{
    assert(fd==17 && size==256); ++reads;
    if (scenario==READ_FAIL) { errno=EIO; return -1; }
    if (scenario==BAD_EVENT) return 15;
    if (reads==1) {
        DtBtNativeEvent e={.type=0x18,.status=0,.peer_token=UINT64_C(0x112233445566)};
        memcpy(data,&e,sizeof(e));
        DtBtNativeEvent foreign={.type=1,.status=0xc2,.peer_token=UINT64_C(0xaabbccddeeff)};
        memcpy((uint8_t*)data+sizeof(e),&foreign,sizeof(foreign)); return 2*sizeof(e);
    }
    errno=EAGAIN; return -1;
}
int dt_test_kqueue(void) { return scenario==KQUEUE_FAIL ? -1 : 18; }
int dt_test_kevent(int fd,const struct kevent *changes,int nchanges,struct kevent *events,int nevents,const struct timespec *timeout)
{
    assert(fd==18);
    if(nchanges) {
        assert(nchanges==1 && changes->ident==17 && changes->filter==EVFILT_READ && changes->flags==EV_ADD);
        assert(!events && nevents==0 && !timeout);
        return scenario==ADD_FAIL ? -1 : 0;
    }
    assert(!changes && nevents==1 && timeout->tv_sec==0 && timeout->tv_nsec==10000000);
    ++waits; now+=10000;
    if(scenario==WAIT_FAIL) { errno=EIO; return -1; }
    if(scenario==WAIT_TIMEOUT || waits>1) return 0;
    memset(events,0,sizeof(*events)); events->ident=scenario==READY_WRONG_FD ? 19 : 17;
    events->filter=EVFILT_READ; events->data=1; /* readiness data is not proven to be a byte count */
    if(scenario==READY_ERROR) events->flags=EV_ERROR;
    return 1;
}
int dt_test_close(int fd) {
    if(fd==18) { ++queue_closes; return 0; }
    assert(fd==17); ++closes; return scenario==CLOSE_FAIL ? -1 : 0;
}
int main(void)
{
    for (scenario=OK;scenario<=READY_WRONG_FD;++scenario) {
        now=1; closes=writes=registrations=unregistrations=reads=queue_closes=waits=cache_checks=0; logs[0]=0;
        int ret=dt_ps4_bluetooth_native_probe();
        assert(ret == (scenario==OK || scenario==WAIT_TIMEOUT ? 0 : scenario==UNREGISTER_FAIL || scenario==CLOSE_FAIL ? 2 : 1));
        assert(closes==(scenario!=OPEN_FAIL));
        assert(writes==(scenario!=OPEN_FAIL));
        assert(registrations==(scenario!=OPEN_FAIL && scenario!=WRITE_SHORT));
        assert(unregistrations==(scenario!=OPEN_FAIL && scenario!=WRITE_SHORT && scenario!=REGISTER_FAIL));
        assert(!strstr(logs,"112233") && !strstr(logs,"18838586676582"));
        if (scenario==OK) assert(now==10000001 && reads==1 && strstr(logs,"events=2"));
#ifdef DT_BT_CACHE_ON_EVENT
        int expected_cache=scenario==OK || scenario==UNREGISTER_FAIL || scenario==CLOSE_FAIL;
#ifdef DT_BT_CACHE_AROUND_LISTEN
        int reached_loop=scenario!=OPEN_FAIL && scenario!=WRITE_SHORT && scenario!=REGISTER_FAIL &&
                         scenario!=CLOCK_FAIL && scenario!=KQUEUE_FAIL && scenario!=ADD_FAIL;
        expected_cache+=2*reached_loop;
#endif
        assert(cache_checks==expected_cache);
#endif
#ifdef DT_BT_KNOWN_EVENTS
        if(scenario==OK) assert(strstr(logs,"BT known DS3 summary events=1") && !strstr(logs,"aabbcc"));
#endif
        if(scenario==WAIT_TIMEOUT) assert(reads==0 && waits==1000 && now==10000001);
        if(scenario==ADD_FAIL || scenario==WAIT_FAIL || scenario==READY_ERROR || scenario==READY_WRONG_FD)
            assert(queue_closes==1 && reads==0);
    }
#ifdef DT_BT_IGNORE_NATIVE_DS3
    for(cleanup_mode=1;cleanup_mode<=4;++cleanup_mode) {
        scenario=OK;now=1;closes=writes=registrations=unregistrations=reads=queue_closes=waits=cache_checks=disconnects=0;logs[0]=0;
        assert(dt_ps4_bluetooth_native_probe()==(cleanup_mode==2 || cleanup_mode==3 ? 2 : 0));
        assert(disconnects==(cleanup_mode!=3) && closes==1 && queue_closes==1 && unregistrations==1);
        assert(now<=13000001);
        assert(!strstr(logs,"112233") && !strstr(logs,"aabbcc"));
        if(cleanup_mode==1 || cleanup_mode==4)assert(strstr(logs,"cleanup link absent=1 inspection_ok=1"));
    }
#endif
    puts("PASS native Bluetooth probe: exact ABI, bounded events, failures, unregister/close and peer-token redaction");
}
