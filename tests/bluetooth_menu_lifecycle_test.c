/* Exercise the real application lifetime without filesystem or console calls. */
#define main dt_bluetooth_main
#define open dt_test_open
#define close dt_test_close
#define flock dt_test_flock
#define access dt_test_access
#define unlink dt_test_unlink
#include "../src/app/bluetooth_bridge.c"
#undef main
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { NORMAL, MENU_WAIT, MENU_RECONNECT, PAD_CLOSE_FAIL, BT_CLOSE_FAIL,
       CREATE_FAIL, WAIT_FAIL, PAD_OPEN_FAIL, STREAM_FAIL, STOP_DURING_CLEANUP };
static int scenario, creates, waits, opens, runs, pad_closes, destroys;
static int live_bt, live_pad, stop_file, finished, logged_status;
static uint64_t logged_valid;
static const DtTransportOps ops={0};
int dt_test_open(const char *path,int flags,...)
{ (void)flags;assert(strstr(path,".lock"));return 42; }
int dt_test_close(int fd) {assert(fd==42);return 0;}
int dt_test_flock(int fd,int flags) {(void)flags;assert(fd==42);return 0;}
int dt_test_access(const char *path,int flags)
{(void)flags;assert(!strcmp(path,STOP_FILE));return stop_file ? 0 : -1;}
int dt_test_unlink(const char *path)
{assert(!strcmp(path,STOP_FILE));stop_file=0;return 0;}
int dt_log_open_named(const char *path) {assert(!strcmp(path,"bluetooth-bridge.log"));return 0;}
void dt_log_close(void) {assert(!live_bt && !live_pad);}
uint32_t kernel_get_fw_version(void) {return 0x13520001;}
void dt_log(const char *fmt,...)
{
    if(strstr(fmt,"bridge finished status=")) {
        va_list args;va_start(args,fmt);
        logged_status=va_arg(args,int);logged_valid=va_arg(args,unsigned long long);
        va_end(args);++finished;
    }
}
int dt_ps4_bluetooth_create(DtTransport *t)
{
    assert(!live_bt && !live_pad && !t->ops);++creates;
    if(scenario==CREATE_FAIL)return -1;
    live_bt=1;*t=(DtTransport){.ops=&ops};return 0;
}
int dt_ps4_bluetooth_status(const DtTransport *t)
{assert(live_bt && t->ops==&ops);return scenario==STREAM_FAIL ? -1 : 0;}
int dt_ps4_bluetooth_destroy(DtTransport *t)
{
    assert(live_bt && !live_pad && t->ops==&ops);++destroys;live_bt=0;*t=(DtTransport){0};
    if(scenario==STOP_DURING_CLEANUP)stop_file=1;
    return scenario==BT_CLOSE_FAIL ? -1 : 0;
}
int dt_bridge_wait_for_input(DtTransport *t,const DtBridgeConfig *c)
{
    assert(live_bt && !live_pad && t->ops==&ops && c->context==t);++waits;
    if(scenario==WAIT_FAIL)return -1;
    if(scenario==MENU_WAIT && waits==2)return 0;
    if(waits>1)assert(pad_closes==waits-1 && destroys==waits-1);
    return 1;
}
int dt_virtual_open(DtVirtualPad **p)
{
    assert(live_bt && !live_pad && !*p);++opens;
    if(scenario==PAD_OPEN_FAIL)return -1;
    live_pad=1;*p=(DtVirtualPad *)1;return 0;
}
int dt_virtual_close(DtVirtualPad *p)
{
    assert(live_bt && live_pad && p==(DtVirtualPad *)1);++pad_closes;live_pad=0;
    return scenario==PAD_CLOSE_FAIL ? -1 : 0;
}
int dt_bridge_run(DtTransport *t,DtVirtualPad *p,const DtBridgeConfig *c,DtBridgeStats *s)
{
    assert(live_bt && live_pad && t->ops==&ops && p==(DtVirtualPad *)1 && c->context==t);++runs;
    *s=(DtBridgeStats){.valid=10,.sent=20,.connections=1,.elapsed_us=1000000};
    if(scenario==NORMAL || scenario==STREAM_FAIL)return 0;
    if(scenario==MENU_RECONNECT && runs==3)return 0;
    return DT_BRIDGE_POWER_OFF;
}
int main(void)
{
    for(scenario=NORMAL;scenario<=STOP_DURING_CLEANUP;++scenario) {
        creates=waits=opens=runs=pad_closes=destroys=0;
        live_bt=live_pad=stop_file=finished=0;logged_valid=0;
        int result=dt_bluetooth_main();
        assert(!live_bt && !live_pad && finished==1 && logged_status==result);
        assert(logged_valid==(uint64_t)runs*10);
        if(scenario==MENU_WAIT)assert(!result && creates==2 && runs==1 && destroys==2);
        else if(scenario==MENU_RECONNECT)assert(!result && creates==3 && runs==3 && pad_closes==3 && destroys==3);
        else if(scenario==PAD_CLOSE_FAIL)assert(result==5 && creates==1 && destroys==1);
        else if(scenario==BT_CLOSE_FAIL)assert(result==6 && creates==1);
        else if(scenario==CREATE_FAIL)assert(result==1 && !waits && !destroys);
        else if(scenario==WAIT_FAIL || scenario==PAD_OPEN_FAIL)assert(result==1 && !runs && destroys==1);
        else if(scenario==STREAM_FAIL)assert(result==7 && creates==1 && destroys==1);
        else if(scenario==STOP_DURING_CLEANUP)assert(!result && creates==1);
        else assert(!result && creates==1 && pad_closes==1 && destroys==1);
    }
    puts("PASS Bluetooth app: native power-off, two reactivations, cleanup order, aggregate stats, stop during cleanup and failures prohibit restart");
}
