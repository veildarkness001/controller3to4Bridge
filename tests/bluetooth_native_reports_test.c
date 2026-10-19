#include "../src/platform/ps4/bluetooth_native_abi.h"
#include "dt/log.h"
#include "dt/bluetooth.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

static unsigned submissions,dequeues,outputs;
static uint64_t now=1000000;
static int scenario;
static char logs[4096];
static const uint64_t peer=UINT64_C(0x112233445566);
uint64_t dt_now_us(void) {return now;}
void dt_log(const char *fmt,...) {va_list a;va_start(a,fmt);size_t n=strlen(logs);vsnprintf(logs+n,sizeof(logs)-n,fmt,a);va_end(a);}
int dt_test_ioctl(int fd,unsigned long command,...) {
 assert(fd==17);va_list a;va_start(a,command);DtBtNativeReportRequest *r=va_arg(a,DtBtNativeReportRequest*);va_end(a);
 assert(r->peer==peer);
 for(unsigned i=0;i<7;++i)assert(!r->reserved[i]);
 if(command==DT_BT_SET_OUTPUT) {
  const uint8_t *b=r->buffer;
  assert(r->size==49 && r->report_id==1 && b[0]==1 && b[10]==2);
  for(unsigned i=1;i<10;++i)assert(!b[i]);
  ++outputs;return scenario==7 ? -1 : 0;
 }
 if(command==DT_BT_SET_FEATURE) {
  ++submissions;const uint8_t expected[]={0xf4,0x42,3,0,0};
  assert(r->size==5 && r->report_id==0xf4 && !memcmp(r->buffer,expected,5));return scenario==1 ? -1 : 0;
 }
 assert(command==DT_BT_DEQUEUE && r->size==80 && !r->report_id);++dequeues;
 if(scenario==2)return -1;
 uint8_t *p=r->buffer;p[0]=49;p[1]=0x1f;p[2]=1;p[4]=(uint8_t)(dequeues-1);
 if(scenario==3)p[0]=48;
 if(scenario==4)p[0]=255;
 if(scenario==5)p[1]=0x1c;
 if(scenario==6)p[2]=2;
 return 0;
}
void dt_bt_report_probe_event(int,uint64_t,const DtBtNativeEvent*,DtBtReportProbe*);
static void event(DtBtReportProbe *p,uint8_t type,uint8_t status) {
 DtBtNativeEvent e={.type=type,.status=status,.peer_token=peer};dt_bt_report_probe_event(17,peer,&e,p);
}
int main(void) {
 for(scenario=0;scenario<=6;++scenario) {
  DtBtReportProbe p={0};submissions=dequeues=0;logs[0]=0;
  event(&p,0x18,0);event(&p,0x1f,0);assert(!submissions && !dequeues);
  DtBtNativeEvent foreign={.type=0x1a,.peer_token=UINT64_C(0xaabbccddeeff)};
  dt_bt_report_probe_event(17,peer,&foreign,&p);assert(!submissions);
  event(&p,0x1a,1);assert(!submissions);
  event(&p,0x1a,0);event(&p,0x1a,0);assert(submissions==1 && p.attempts==1 && !p.completed);
  event(&p,0x1e,0);assert(p.completed==(scenario!=1));
  event(&p,0x1f,0);event(&p,0x1f,0);
  assert(dequeues==(scenario==1 ? 0 : 2));
  assert(p.valid==(scenario==0 ? 2u : 0u));
  if(scenario==0)assert(p.changes==1 && p.last.timestamp_us==1000000 && p.completions==1);
  if(scenario>=3)assert(p.invalid==2 && !p.errors);
  if(scenario==2)assert(p.errors==2);
  if(scenario==1)assert(p.errors==1);
  event(&p,0x19,0);event(&p,0x1f,0);assert(!p.peer && !p.completed);
  event(&p,0x1a,0);assert(submissions==2);
  assert(!strstr(logs,"112233") && !strstr(logs,"aabbcc"));
 }
 scenario=0;DtBtReportProbe p={0};event(&p,0x1a,0);event(&p,0x1e,0x83);
 assert(!p.completed && p.errors==1 && p.completions==1);
 /* LED command framing, bounds and all four player masks. */
 uint8_t hid[51],raw[51],before[51];memset(before,0xcc,sizeof(before));
 for(unsigned player=1;player<=4;++player) {
  memset(hid,0xcc,sizeof(hid));memset(raw,0xcc,sizeof(raw));
  assert(dt_ds3_bluetooth_player_led(hid,49,DT_BT_HID_REPORT,player)==49);
  assert(dt_ds3_bluetooth_player_led(raw,50,DT_BT_HIDP_SDU,player)==50);
  assert(raw[0]==0x52 && !memcmp(raw+1,hid,49) && hid[49]==0xcc && raw[50]==0xcc);
  for(unsigned i=0;i<49;++i) {
   uint8_t expected=0;
   if(i==0)expected=1;
   if(i==10)expected=(uint8_t)(1u<<player);
   if(i>=11 && i<31) {
    const uint8_t timing[]={0xff,0x27,0x10,0,0x32};expected=timing[(i-11)%5];
   }
   assert(hid[i]==expected);
  }
 }
 memcpy(hid,before,sizeof(hid));
 assert(!dt_ds3_bluetooth_player_led(hid,48,DT_BT_HID_REPORT,1));
 assert(!dt_ds3_bluetooth_player_led(hid,49,DT_BT_HIDP_SDU,1));
 assert(!dt_ds3_bluetooth_player_led(hid,51,0,1));
 assert(!dt_ds3_bluetooth_player_led(hid,51,DT_BT_HID_REPORT,0));
 assert(!dt_ds3_bluetooth_player_led(hid,51,DT_BT_HID_REPORT,5));
 assert(!dt_ds3_bluetooth_player_led(NULL,51,DT_BT_HID_REPORT,1));
 assert(!memcmp(hid,before,sizeof(hid)));
 /* Optional output never blocks or faults input, even on ioctl failure. */
 for(scenario=0;scenario<=7;scenario+=7) {
  p=(DtBtReportProbe){0};outputs=dequeues=0;now=1000000;
  dt_bt_report_player_led(17,NULL);dt_bt_report_player_led(17,&p);assert(!outputs);
  event(&p,0x1a,0);event(&p,0x1f,0);
  dt_bt_report_player_led(17,&p);assert(!outputs); /* No F4 completion. */
  event(&p,0x1e,0);p.latest.size=0;now+=150000;
  dt_bt_report_player_led(17,&p);assert(!outputs); /* No current input. */
  event(&p,0x1f,0);now=0;dt_bt_report_player_led(17,&p);assert(!outputs);
  now=999999;dt_bt_report_player_led(17,&p);assert(!outputs);
  now=1149999;dt_bt_report_player_led(17,&p);assert(!outputs);
  now=1150000;dt_bt_report_player_led(17,&p);assert(outputs==1 && p.led_attempted && !p.errors);
  dt_bt_report_player_led(17,&p);assert(outputs==1);
  event(&p,0x1e,0x83);assert(p.completed && !p.errors && p.completions==1);
  event(&p,0x1f,0);assert(p.valid==3 && !p.errors);
  event(&p,0x19,0);dt_bt_report_player_led(17,&p);assert(outputs==1 && !p.led_attempted);
  event(&p,0x1a,0);event(&p,0x1e,0);event(&p,0x1f,0);
  now+=150000;dt_bt_report_player_led(17,&p);assert(outputs==2 && !p.errors);
 }
 puts("PASS native reports: ready-only F4, exact ABI/payload, asynchronous completion, peer isolation, reconnect, bounded dequeue and real DS3 decode");
}
