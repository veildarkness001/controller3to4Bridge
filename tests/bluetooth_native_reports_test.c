#include "../src/platform/ps4/bluetooth_native_abi.h"
#include "dt/log.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

static unsigned submissions,dequeues;
static int scenario;
static char logs[4096];
static const uint64_t peer=UINT64_C(0x112233445566);
uint64_t dt_now_us(void) {return 1000000;}
void dt_log(const char *fmt,...) {va_list a;va_start(a,fmt);size_t n=strlen(logs);vsnprintf(logs+n,sizeof(logs)-n,fmt,a);va_end(a);}
int dt_test_ioctl(int fd,unsigned long command,...) {
 assert(fd==17);va_list a;va_start(a,command);DtBtNativeReportRequest *r=va_arg(a,DtBtNativeReportRequest*);va_end(a);
 assert(r->peer==peer);
 for(unsigned i=0;i<7;++i)assert(!r->reserved[i]);
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
 puts("PASS native reports: ready-only F4, exact ABI/payload, asynchronous completion, peer isolation, reconnect, bounded dequeue and real DS3 decode");
}
