#define open dt_test_open
#define close dt_test_close
#define read dt_test_read
#define write dt_test_write
#define ioctl dt_test_ioctl
#define kqueue dt_test_kqueue
#define kevent dt_test_kevent
#define usleep dt_test_usleep
#include "../src/platform/ps4/dualsense_bluetooth.c"
#include "dualsense_fixtures.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

static int scenario,live_fd,live_queue,caps,window,registered,outputs,dequeues,link_present;
static int packet_kind,output_fail;
static uint64_t now=1000000;
static char logs[8192];
static const uint64_t peer=UINT64_C(0x112233445566);
uint64_t dt_now_us(void) {now+=100;return now;}
void dt_log(const char *fmt,...)
{va_list a;va_start(a,fmt);size_t n=strlen(logs);if(n<sizeof(logs)-1)vsnprintf(logs+n,sizeof(logs)-n,fmt,a);va_end(a);}
int dt_test_usleep(unsigned us) {now+=us;return 0;}
int dt_bt_event_window_begin(DtBtEventWindow *w) {w->attempted=1;caps=1;return scenario==1?-1:0;}
int dt_bt_event_window_end(DtBtEventWindow *w) {w->attempted=0;caps=0;return scenario==9?-1:0;}
int dt_dualsense_bt_window_begin(DtDualsenseBtWindow *w) {w->attempted=1;window=1;return scenario==2?-1:0;}
int dt_dualsense_bt_window_end(DtDualsenseBtWindow *w) {w->attempted=0;window=0;return scenario==10?-1:0;}
int dt_test_open(const char *path,int flags,...)
{assert(!strcmp(path,"/dev/bt") && (flags&O_NONBLOCK));if(scenario==3)return -1;live_fd=1;return 17;}
int dt_test_close(int fd) {if(fd==18)live_queue=0;else {assert(fd==17);live_fd=0;}return 0;}
ssize_t dt_test_write(int fd,const void *data,size_t size)
{assert(fd==17 && size==8 && *(const uint64_t*)data==DT_BT_HID_EVENT_MASK);return scenario==4?7:8;}
ssize_t dt_test_read(int fd,void *out,size_t size) {(void)fd;(void)out;(void)size;return -1;}
int dt_test_kqueue(void) {if(scenario==6)return -1;live_queue=1;return 18;}
int dt_test_kevent(int fd,const struct kevent *changes,int count,struct kevent *out,int capacity,const struct timespec *timeout)
{(void)out;(void)capacity;(void)timeout;assert(fd==18);if(changes){assert(count==1);return scenario==7?-1:0;}return 0;}
int dt_ps4_bluetooth_link_present(uint64_t known) {assert(known==peer);return link_present;}
int dt_test_ioctl(int fd,unsigned long command,...)
{
    assert(fd==17);va_list a;va_start(a,command);void *arg=va_arg(a,void*);va_end(a);
    if(command==DT_BT_REGISTER_DS3 || command==DT_BT_UNREGISTER_DS3) {
        DtBtNativeRegistration *r=arg;assert(r->vendor==0x054c && r->product==0x0ce6);
        if(command==DT_BT_REGISTER_DS3){if(scenario==5)return -1;registered=1;}
        else {registered=0;if(scenario==8)return -1;}
        return 0;
    }
    if(command==DT_BT_DISCONNECT) {assert(*(uint64_t*)arg==peer);link_present=0;return 0;}
    DtBtNativeReportRequest *r=arg;assert((r->peer&PEER_MASK)==peer);
    if(command==DT_BT_SET_OUTPUT) {
        assert(r->size==78 && r->report_id==0x31 && dt_dualsense_check_crc(0xa2,r->buffer,78)==0);
        ++outputs;return output_fail?-1:0;
    }
    assert(command==DT_BT_DEQUEUE && r->size==80);++dequeues;
    if(packet_kind==3)return -1;
    uint8_t *p=r->buffer;p[1]=0x1f;
    if(packet_kind==0) {
        const uint8_t basic[]={1,128,128,128,128,0x28,0,0,0,0};p[0]=10;memcpy(p+2,basic,10);
    } else {p[0]=78;memcpy(p+2,bt_neutral,78);if(packet_kind==2)p[22]^=1;}
    return 0;
}
static void reset(void)
{scenario=live_fd=live_queue=caps=window=registered=outputs=dequeues=link_present=packet_kind=output_fail=0;logs[0]=0;}
static void event(DualsenseNative *b,uint8_t type,uint64_t token,uint32_t model)
{DtBtNativeEvent e={.type=type,.peer_token=token,.vendor_product=model};handle_event(b,&e);}
int main(void)
{
    for(int failure=0;failure<=10;++failure) {
        reset();scenario=failure;DtTransport t={0};int rc=dt_ps4_dualsense_bluetooth_create(&t);
        if(failure>=1 && failure<=7)assert(rc==-1 && !t.ops);
        else {
            assert(rc==0 && caps && window && registered && live_fd && live_queue);
            assert(dt_ps4_dualsense_bluetooth_status(&t)==0);
            int result=dt_ps4_dualsense_bluetooth_destroy(&t);
            assert(result==(failure>=8?1:0) && !t.ops);
        }
        assert(!caps && !window && !registered && !live_fd && !live_queue);
    }
    reset();DualsenseNative b={.fd=17,.queue=18};
    event(&b,0x1a,peer,0x05c4054c);event(&b,0x1f,peer,0);assert(!b.known && !dequeues);
    event(&b,0x18,peer,0x0ce6054c);assert(b.known==peer);
    event(&b,0x1a,peer|UINT64_C(0x1000000000000),0);assert(b.token);
    event(&b,0x1f,peer|UINT64_C(0x2000000000000),0);assert(!dequeues);
    event(&b,0x1f,b.token,0);
#if defined(DT_DUALSENSE_BT_ENHANCED_REQUEST) && !defined(DT_DUALSENSE_BT_PASSIVE)
    const int expected_outputs=1;
#else
    const int expected_outputs=0;
#endif
    assert(b.valid==1 && b.basic==1 && outputs==expected_outputs && !b.fault);
    assert(b.own_reports==2 && b.token_mismatches==1 && b.dequeues==1);
    DtInputReport r;assert(read_report(&b,&r,4)==1 && r.size==10 && b.delivered==1);
    assert(read_report(&b,&r,4)==0);
    packet_kind=1;event(&b,0x1f,b.token,0);assert(b.full==1 && b.valid==2 && outputs==expected_outputs);
    packet_kind=2;event(&b,0x1f,b.token,0);assert(b.invalid==1 && b.valid==2 && !b.fault);
    event(&b,0x19,b.token,0);assert(!b.token && !b.latest.size && b.known==peer);
    event(&b,0x1a,peer,0);output_fail=1;packet_kind=0;event(&b,0x1f,peer,0);assert(outputs==2*expected_outputs && !b.fault);
    packet_kind=3;event(&b,0x1f,peer,0);assert(b.fault);
    assert(!strstr(logs,"112233") && !strstr(logs,"445566"));
    reset();DtTransport t={0};assert(dt_ps4_dualsense_bluetooth_create(&t)==0);
    DualsenseNative *owned=t.context;owned->known=peer;link_present=1;
    DtDualsenseBtStats stats={0};owned->own_reports=7;owned->token_mismatches=3;
    assert(dt_ps4_dualsense_bluetooth_stats(&t,&stats)==0 && stats.own_reports==7 && stats.token_mismatches==3);
    assert(dt_ps4_dualsense_bluetooth_stats(&t,NULL)==-1);
    assert(dt_ps4_dualsense_bluetooth_matches(&t));
    assert(dt_ps4_dualsense_bluetooth_quiesce(&t)==0 && !link_present && owned->suspended);
    assert(dt_ps4_dualsense_bluetooth_destroy(&t)==0 && !link_present);
    assert(!dt_ps4_dualsense_bluetooth_matches(&t));
    assert(dt_ps4_dualsense_bluetooth_stats(&t,&stats)==-1);
    puts("PASS native DualSense BT: setup failures, peer/model filtering, basic/full/CRC input, optional mode request and cleanup");
}
