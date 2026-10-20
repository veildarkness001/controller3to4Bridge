#include "dt/ps4_bluetooth.h"
#include "dt/ps4_bluetooth_probe.h"
#include "dt/log.h"
#include "bluetooth_native_abi.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <unistd.h>

typedef struct {
    int fd,queue,registered,fault,suspended;
    uint64_t known;
    unsigned delivered;
    DtBtEventWindow capabilities;
    DtBtProfileWindow profile;
    DtBtReportProbe reports;
} NativeBluetooth;
static const DtTransportOps native_ops;

/* Keep the listener across short recovery attempts: HCI/L2CAP setup can take
 * longer than one session retry. All waits use the caller's deadline. */
static int pump(NativeBluetooth *b,unsigned timeout_ms)
{
    uint64_t start=dt_now_us(),last=start;
    if(!start || timeout_ms>1000) return -1;
    for(;;) {
        uint64_t now=dt_now_us();
        if(!now || now<last) return -1;
        last=now;
        uint64_t elapsed=now-start,budget=(uint64_t)timeout_ms*1000;
        if(elapsed>=budget) return 0;
        uint64_t remaining=budget-elapsed;
        struct timespec timeout={.tv_sec=0,.tv_nsec=(long)(remaining*1000)};
        struct kevent ready;
        int count=kevent(b->queue,NULL,0,&ready,1,&timeout);
        if(count<0 && errno==EINTR)continue;
        if(!count)return 0;
        if(count!=1 || ready.ident!=(uintptr_t)b->fd || ready.filter!=EVFILT_READ ||
           (ready.flags&(EV_ERROR|EV_EOF)))return -1;
        DtBtNativeEvent events[16];
        ssize_t size=read(b->fd,events,sizeof(events));
        if(size<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))continue;
        if(size<=0 || (size_t)size>sizeof(events) || size%sizeof(events[0]))return -1;
        for(unsigned i=0;i<(unsigned)size/sizeof(events[0]);++i) {
            if(b->suspended)continue;
            if((events[i].peer_token&UINT64_C(0xffffffffffff))==b->known &&
               (events[i].type==0x1a || events[i].type==0x19 || events[i].type==6))
                dt_log("BT own link event type=0x%02x status=0x%02x\n",events[i].type,events[i].status);
            dt_bt_report_probe_event(b->fd,b->known,&events[i],&b->reports);
        }
        if(b->reports.errors || b->reports.invalid)return -1;
        if(b->suspended || !b->reports.peer || b->reports.valid>b->delivered)return 1;
    }
}
static int quiesce(NativeBluetooth *b)
{
    if(b->fault)return -1;
    b->suspended=1;
    int present=dt_ps4_bluetooth_link_present(b->known);
    if(present==1) {
        uint64_t peer=b->known;
        int result=ioctl(b->fd,DT_BT_DISCONNECT,&peer);
        dt_log("BT USB handoff disconnect result=0x%08x\n",(uint32_t)result);
        for(unsigned i=0;i<150 && present==1;++i) {
            usleep(20000);present=dt_ps4_bluetooth_link_present(b->known);
        }
    }
    dt_log("BT USB handoff link absent=%d inspection_ok=%d\n",present==0,present>=0);
    if(present!=0) {b->fault=1;return -1;}
    /* Never let an old ready/completion event reactivate a previous stream.
     * Keep this bounded even if the kernel continues producing events. */
    int drained=0;
    for(unsigned i=0;i<64;++i) {
        int result=pump(b,1);
        if(result<0)break;
        if(!result) {drained=1;break;}
    }
    if(!drained) {b->fault=1;return -1;}
    b->reports.peer=0;b->reports.accepted=b->reports.completed=0;
    b->reports.led_attempted=0;b->reports.enabled_us=0;
    memset(&b->reports.latest,0,sizeof(b->reports.latest));
    memset(&b->reports.last,0,sizeof(b->reports.last));
    b->delivered=b->reports.valid;
    dt_log("BT stream cleared for USB; native listener retained\n");
    return 0;
}
static int reconnect(void *context)
{
    NativeBluetooth *b=context;
    if(b->fault)return -1;
    if(b->suspended) {
        if(quiesce(b)!=0)return -1;
        b->suspended=0;
        dt_log("BT listener rearmed after USB; press PS on paired DS3\n");
    }
    if(pump(b,250)<0) {b->fault=1;return -1;}
    dt_bt_report_player_led(b->fd,&b->reports);
    uint64_t now=dt_now_us();
    return b->reports.completed && b->reports.latest.size==49 && now &&
        now>=b->reports.latest.timestamp_us && now-b->reports.latest.timestamp_us<250000 ? 1 : 0;
}
static int read_report(void *context,DtInputReport *out,unsigned timeout_ms)
{
    NativeBluetooth *b=context;
    if(!out || !timeout_ms || timeout_ms>1000 || b->fault || !b->reports.peer)return -1;
    if(b->reports.valid==b->delivered && pump(b,timeout_ms)<0) {b->fault=1;return -1;}
    if(!b->reports.peer)return -1;
    dt_bt_report_player_led(b->fd,&b->reports);
    if(b->reports.valid>b->delivered && b->reports.latest.size) {
        *out=b->reports.latest;b->delivered=b->reports.valid;return 1;
    }
    return 0;
}
static int cleanup(NativeBluetooth *b)
{
    int status=0;
    if(b->registered && b->known) {
        int present=dt_ps4_bluetooth_link_present(b->known);
        if(present==1) {
            uint64_t peer=b->known;
            int result=ioctl(b->fd,DT_BT_DISCONNECT,&peer);
            dt_log("BT cleanup disconnect result=0x%08x\n",(uint32_t)result);
            for(unsigned i=0;i<150 && present==1;++i) {
                usleep(20000);present=dt_ps4_bluetooth_link_present(b->known);
            }
        }
        dt_log("BT cleanup link absent=%d inspection_ok=%d\n",present==0,present>=0);
        if(present!=0)status=1;
    }
    if(b->queue>=0) {int r=close(b->queue);dt_log("BT cleanup kqueue=%d\n",r);if(r)status=1;b->queue=-1;}
    if(b->registered) {
        DtBtNativeRegistration request={0x054c,0x0268};int restored=0;
        int r=dt_bt_admitted_ioctl(b->fd,DT_BT_UNREGISTER_DS3,&request,&restored);
        dt_log("BT cleanup unregister=0x%08x original_instructions=%d\n",(uint32_t)r,restored);
        if(r || !restored)status=1;b->registered=0;
    }
    if(b->fd>=0) {int r=close(b->fd);dt_log("BT cleanup close=%d\n",r);if(r)status=1;b->fd=-1;}
    if(dt_bt_profile_window_end(&b->profile)!=0)status=1;
    if(dt_bt_event_window_end(&b->capabilities)!=0)status=1;
    dt_log("BT reports valid=%u invalid=%u changes=%u errors=%u F4_completed=%u cleanup_status=%d\n",
        b->reports.valid,b->reports.invalid,b->reports.changes,b->reports.errors,b->reports.completions,status);
    return status;
}
static void close_transport(void *context)
{ NativeBluetooth *b=context;(void)cleanup(b);free(b); }
static const DtTransportOps native_ops={.read_report=read_report,.reconnect=reconnect,.close=close_transport};
int dt_ps4_bluetooth_status(const DtTransport *t)
{
    if(!t || t->kind!=DT_TRANSPORT_BLUETOOTH || t->ops!=&native_ops || !t->context)return -1;
    const NativeBluetooth *b=t->context;return b->fault ? -1 : 0;
}
int dt_ps4_bluetooth_service(DtTransport *t,unsigned timeout_ms)
{
    if(dt_ps4_bluetooth_status(t)!=0)return -1;
    NativeBluetooth *b=t->context;
    if(pump(b,timeout_ms)<0) {b->fault=1;return -1;}
    return 0;
}
int dt_ps4_bluetooth_quiesce(DtTransport *t)
{
    if(dt_ps4_bluetooth_status(t)!=0)return -1;
    return quiesce(t->context);
}
int dt_ps4_bluetooth_destroy(DtTransport *t)
{
    if(!t || t->kind!=DT_TRANSPORT_BLUETOOTH || t->ops!=&native_ops || !t->context)return -1;
    NativeBluetooth *b=t->context;int status=cleanup(b);free(b);*t=(DtTransport){0};return status;
}
int dt_ps4_bluetooth_create(DtTransport *out)
{
    if(!out || out->ops)return -1;
    NativeBluetooth *b=calloc(1,sizeof(*b));if(!b)return -1;
    b->fd=b->queue=-1;
    if(dt_bt_event_window_begin(&b->capabilities)!=0 || dt_ps4_bluetooth_known_peer(&b->known)!=0)goto fail;
    b->fd=open("/dev/bt",O_RDWR|O_NONBLOCK);if(b->fd<0)goto fail;
    uint64_t mask=DT_BT_HID_EVENT_MASK;
    if(write(b->fd,&mask,8)!=8)goto fail;
    DtBtNativeRegistration request={0x054c,0x0268};int restored=0;
    int result=dt_bt_admitted_ioctl(b->fd,DT_BT_REGISTER_DS3,&request,&restored);
    b->registered=result==0;
    if(result || !restored || dt_bt_profile_window_begin(&request,&b->profile)!=0)goto fail;
    b->queue=kqueue();if(b->queue<0)goto fail;
    struct kevent change;EV_SET(&change,b->fd,EVFILT_READ,EV_ADD,0,0,NULL);
    if(kevent(b->queue,&change,1,NULL,0,NULL)!=0)goto fail;
    *out=(DtTransport){.kind=DT_TRANSPORT_BLUETOOTH,.ops=&native_ops,.context=b};
    dt_log("BT native listener ready; awaiting PS on paired DS3\n");return 0;
fail:;
    dt_log("BT native setup failed; cleanup follows\n");
    int failed_cleanup=cleanup(b);free(b);return failed_cleanup ? -2 : -1;
}
