#include "dt/ps4_dualsense_bluetooth.h"
#include "dt/ps4_bluetooth_probe.h"
#include "dt/dualsense.h"
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

#define PEER_MASK UINT64_C(0xffffffffffff)
typedef struct {
    int fd, queue, registered, fault, enable_attempted, suspended;
    uint64_t known, token;
    unsigned valid, delivered, invalid, basic, full;
    unsigned own_reports, token_mismatches, dequeues;
    DtInputReport latest;
    DtBtEventWindow capabilities;
    DtDualsenseBtWindow window;
} DualsenseNative;
static const DtTransportOps native_ops;
static int matches_model(uint32_t packed)
{
    return packed==UINT32_C(0x0ce6054c) || packed==UINT32_C(0x054c0ce6);
}
static void handle_event(DualsenseNative *b,const DtBtNativeEvent *event)
{
    if(b->suspended)return;
    uint64_t identity=event->peer_token&PEER_MASK;
    if(event->type>=0x18 && event->type<=0x20 && event->type!=0x1f)
        dt_log("DualSense BT event type=0x%02x status=0x%02x model_match=%d own=%d\n",
               event->type,event->status,matches_model(event->vendor_product),
               b->known && identity==b->known);
    /* Bind only model-tagged connection events. Never adopt an arbitrary
     * queue peer or the physical DS4, and never log/persist peer tokens. */
    if(!b->known && !event->status && identity &&
       (event->type==0x18 || event->type==0x1a) && matches_model(event->vendor_product))
        b->known=identity;
    if(!b->known || identity!=b->known)return;
    if(event->type==0x1a && !event->status) {
        b->token=event->peer_token;b->enable_attempted=0;b->latest.size=0;
    } else if(event->type==6 || event->type==0x19) {
        b->token=0;b->latest.size=0;b->delivered=b->valid;
    } else if(event->type==0x1f && b->token) {
        ++b->own_reports;
        if(b->token!=event->peer_token) {++b->token_mismatches;return;}
        uint8_t packet[80]={0};
        DtBtNativeReportRequest request={.peer=b->token,.buffer=packet,.size=sizeof(packet)};
        ++b->dequeues;
        int result=ioctl(b->fd,DT_BT_DEQUEUE,&request);
        if(result<0) {b->fault=1;return;}
        size_t size=packet[0];DtInputReport report;
        if(packet[1]!=0x1f || size>sizeof(packet)-2 ||
           dt_dualsense_bluetooth_unpack(packet+2,size,DT_BT_HID_REPORT,dt_now_us(),&report)!=0) {
            ++b->invalid;return;
        }
        b->latest=report;++b->valid;
        if(size==10)++b->basic;else ++b->full;
        if(b->valid==1 || (size==78 && b->full==1))
            dt_log("DualSense BT valid input bytes=%zu basic=%u full=%u\n",size,b->basic,b->full);
#if defined(DT_DUALSENSE_BT_ENHANCED_REQUEST) && !defined(DT_DUALSENSE_BT_PASSIVE)
        if(!b->enable_attempted) {
            uint8_t output[78];b->enable_attempted=1;
            size_t length=dt_dualsense_bluetooth_enable(output,sizeof(output));
            if(length!=sizeof(output)) {b->fault=1;return;}
            DtBtNativeReportRequest enable={.peer=b->token,.buffer=output,.size=length,.report_id=0x31};
            result=ioctl(b->fd,DT_BT_SET_OUTPUT,&enable);
            dt_log("DualSense BT enhanced-mode output submitted result=0x%08x; full input required for confirmation\n",(uint32_t)result);
        }
#endif
    }
}
static int pump(DualsenseNative *b,unsigned timeout_ms)
{
    uint64_t start=dt_now_us(),previous=start;
    if(!start || !timeout_ms || timeout_ms>1000)return -1;
    while(!b->fault) {
        uint64_t now=dt_now_us(),budget=(uint64_t)timeout_ms*1000;
        if(!now || now<previous)return -1;
        previous=now;if(now-start>=budget)return 0;
        struct timespec timeout={.tv_sec=0,.tv_nsec=(long)((budget-(now-start))*1000)};
        struct kevent ready;
        int count=kevent(b->queue,NULL,0,&ready,1,&timeout);
        if(count<0 && errno==EINTR)continue;
        if(!count)return 0;
        if(count!=1 || ready.ident!=(uintptr_t)b->fd || ready.filter!=EVFILT_READ ||
           (ready.flags&(EV_ERROR|EV_EOF)))return -1;
        DtBtNativeEvent events[16];ssize_t size=read(b->fd,events,sizeof(events));
        if(size<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))continue;
        if(size<=0 || (size_t)size>sizeof(events) || size%sizeof(events[0]))return -1;
        for(unsigned i=0;i<(unsigned)size/sizeof(events[0]);++i)handle_event(b,&events[i]);
        if(b->fault)return -1;
        if(b->valid>b->delivered)return 1;
    }
    return -1;
}
static int reconnect(void *context)
{
    DualsenseNative *b=context;if(b->fault)return -1;
    b->suspended=0;
    if(pump(b,250)<0) {b->fault=1;return -1;}
    uint64_t now=dt_now_us();
    return b->token && b->latest.size && now>=b->latest.timestamp_us &&
           now-b->latest.timestamp_us<250000?1:0;
}
static int read_report(void *context,DtInputReport *out,unsigned timeout_ms)
{
    DualsenseNative *b=context;
    if(!out || b->fault || !b->token || !timeout_ms || timeout_ms>1000)return -1;
    if(b->valid==b->delivered && pump(b,timeout_ms)<0) {b->fault=1;return -1;}
    if(!b->token)return -1;
    if(b->valid>b->delivered && b->latest.size) {
        *out=b->latest;b->delivered=b->valid;return 1;
    }
    return 0;
}
static int cleanup(DualsenseNative *b)
{
    int status=0;
    if(b->registered && b->known) {
        int present=dt_ps4_bluetooth_link_present(b->known);
        if(present==1) {
            uint64_t peer=b->known;int rc=ioctl(b->fd,DT_BT_DISCONNECT,&peer);
            dt_log("DualSense BT cleanup disconnect result=0x%08x\n",(uint32_t)rc);
            for(unsigned i=0;i<150 && present==1;++i) {
                usleep(20000);present=dt_ps4_bluetooth_link_present(b->known);
            }
        }
        dt_log("DualSense BT cleanup owned link absent=%d inspection_ok=%d\n",present==0,present>=0);
        if(present!=0)status=1;
    }
    if(b->queue>=0) {if(close(b->queue)!=0)status=1;b->queue=-1;}
    if(b->registered) {
        DtBtNativeRegistration request={0x054c,0x0ce6};
        int rc=ioctl(b->fd,DT_BT_UNREGISTER_DS3,&request);
        dt_log("DualSense BT cleanup unregister result=0x%08x\n",(uint32_t)rc);
        if(rc!=0)status=1;b->registered=0;
    }
    if(b->fd>=0) {if(close(b->fd)!=0)status=1;b->fd=-1;}
    if(dt_dualsense_bt_window_end(&b->window)!=0)status=1;
    if(dt_bt_event_window_end(&b->capabilities)!=0)status=1;
    dt_log("DualSense BT cleanup status=%d valid=%u basic=%u full=%u invalid=%u\n",
           status,b->valid,b->basic,b->full,b->invalid);
    dt_log("DualSense BT receive own_events=%u token_mismatches=%u dequeues=%u\n",
           b->own_reports,b->token_mismatches,b->dequeues);
    return status;
}
static void close_transport(void *context)
{DualsenseNative *b=context;(void)cleanup(b);free(b);}
static const DtTransportOps native_ops={.read_report=read_report,.reconnect=reconnect,.close=close_transport};
int dt_ps4_dualsense_bluetooth_status(const DtTransport *t)
{
    if(!t || t->ops!=&native_ops || !t->context)return -1;
    DualsenseNative *b=t->context;return b->fault?-1:0;
}
int dt_ps4_dualsense_bluetooth_destroy(DtTransport *t)
{
    if(!t || t->ops!=&native_ops || !t->context)return -1;
    DualsenseNative *b=t->context;int rc=cleanup(b);free(b);*t=(DtTransport){0};return rc;
}
int dt_ps4_dualsense_bluetooth_create(DtTransport *out)
{
    if(!out || out->ops)return -1;
    DualsenseNative *b=calloc(1,sizeof(*b));if(!b)return -1;b->fd=b->queue=-1;
    if(dt_bt_event_window_begin(&b->capabilities)!=0 || dt_dualsense_bt_window_begin(&b->window)!=0)goto fail;
    b->fd=open("/dev/bt",O_RDWR|O_NONBLOCK);if(b->fd<0)goto fail;
    uint64_t mask=DT_BT_HID_EVENT_MASK;if(write(b->fd,&mask,sizeof(mask))!=sizeof(mask))goto fail;
    DtBtNativeRegistration request={0x054c,0x0ce6};
    int rc=ioctl(b->fd,DT_BT_REGISTER_DS3,&request);b->registered=rc==0;
    dt_log("DualSense BT registration result=0x%08x\n",(uint32_t)rc);
    if(rc!=0)goto fail;
    b->queue=kqueue();if(b->queue<0)goto fail;
    struct kevent change;EV_SET(&change,b->fd,EVFILT_READ,EV_ADD,0,0,NULL);
    if(kevent(b->queue,&change,1,NULL,0,NULL)!=0)goto fail;
    *out=(DtTransport){.kind=DT_TRANSPORT_BLUETOOTH,.ops=&native_ops,.context=b};
    dt_log("DualSense BT listener ready; pairing is performed by user in PS4 settings\n");return 0;
fail:;
    int failed_cleanup=cleanup(b);free(b);return failed_cleanup?-2:-1;
}

int dt_ps4_dualsense_bluetooth_stats(const DtTransport *t,DtDualsenseBtStats *out)
{
    if(!out || !t || t->ops!=&native_ops || !t->context)return -1;
    const DualsenseNative *b=t->context;
    *out=(DtDualsenseBtStats){.valid=b->valid,.basic=b->basic,.full=b->full,
        .invalid=b->invalid,.own_reports=b->own_reports,
        .token_mismatches=b->token_mismatches,.dequeues=b->dequeues};
    return 0;
}

int dt_ps4_dualsense_bluetooth_matches(const DtTransport *t)
{return t && t->kind==DT_TRANSPORT_BLUETOOTH && t->ops==&native_ops && t->context;}
int dt_ps4_dualsense_bluetooth_quiesce(DtTransport *t)
{
    if(dt_ps4_dualsense_bluetooth_status(t)!=0)return -1;
    DualsenseNative *b=t->context;b->suspended=1;
    int present=b->known?dt_ps4_bluetooth_link_present(b->known):0;
    if(present==1) {
        uint64_t peer=b->known;int rc=ioctl(b->fd,DT_BT_DISCONNECT,&peer);
        dt_log("DualSense BT USB handoff disconnect result=0x%08x\n",(uint32_t)rc);
        for(unsigned i=0;i<150 && present==1;++i) {
            usleep(20000);present=dt_ps4_bluetooth_link_present(b->known);
        }
    }
    dt_log("DualSense BT USB handoff link absent=%d inspection_ok=%d\n",present==0,present>=0);
    if(present!=0) {b->fault=1;return -1;}
    int drained=0;
    for(unsigned i=0;i<64;++i) {
        int result=pump(b,1);
        if(result<0)break;
        if(!result) {drained=1;break;}
    }
    if(!drained) {b->fault=1;return -1;}
    b->token=0;b->latest=(DtInputReport){0};b->delivered=b->valid;
    return 0;
}
