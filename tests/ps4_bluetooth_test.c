#include "../src/platform/ps4/bluetooth.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
enum { OK,CAP_FAIL,IDENTITY_FAIL,OPEN_FAIL,WRITE_FAIL,REGISTER_FAIL,PROFILE_FAIL,QUEUE_FAIL,ADD_FAIL,
       MALFORMED,WAIT_FAIL,CLOCK_FAIL,LINK_STUCK,UNREGISTER_FAIL,RESTORE_FAIL,OUTPUT_FAIL,
       CREATE_RESTORE_FAIL };
static unsigned outputs, features;
static int scenario,opens,closes,registrations,unregistrations,cap_restores,profile_restores,live,disconnects;
static int queued,setup;static uint64_t now;
static const uint64_t peer=UINT64_C(0x112233445566);
static char logs[8192];
void dt_log(const char *fmt,...) {va_list a;va_start(a,fmt);size_t n=strlen(logs);vsnprintf(logs+n,sizeof(logs)-n,fmt,a);va_end(a);}
uint64_t dt_now_us(void) {return scenario==CLOCK_FAIL ? 0 : now;}
int dt_bt_event_window_begin(DtBtEventWindow *w) {w->attempted=1;return scenario==CAP_FAIL || scenario==CREATE_RESTORE_FAIL ? 1 : 0;}
int dt_bt_event_window_end(DtBtEventWindow *w) {assert(closes==opens);++cap_restores;w->attempted=0;return scenario==RESTORE_FAIL || scenario==CREATE_RESTORE_FAIL ? -1 : 0;}
int dt_ps4_bluetooth_known_peer(uint64_t *out) {*out=peer;return scenario==IDENTITY_FAIL;}
int dt_ps4_bluetooth_link_present(uint64_t known) {assert(known==peer);return scenario==LINK_STUCK ? 1 : live;}
int dt_bt_profile_window_begin(const DtBtNativeRegistration *r,DtBtProfileWindow *w) {assert(r->vendor==0x054c && r->product==0x0268);w->attempted=1;return scenario==PROFILE_FAIL ? -1 : 0;}
int dt_bt_profile_window_end(DtBtProfileWindow *w) {assert(closes==opens);++profile_restores;w->attempted=0;return 0;}
int dt_bt_admitted_ioctl(int fd,unsigned long command,DtBtNativeRegistration *r,int *restored) {
 assert(fd==17 && r->vendor==0x054c && r->product==0x0268);*restored=1;
 if(command==DT_BT_REGISTER_DS3) {++registrations;return scenario==REGISTER_FAIL ? -1 : 0;}
 assert(command==DT_BT_UNREGISTER_DS3);++unregistrations;return scenario==UNREGISTER_FAIL ? -1 : 0;
}
int dt_test_open(const char *path,int flags,...) {assert(!strcmp(path,"/dev/bt") && flags==(O_RDWR|O_NONBLOCK));if(scenario==OPEN_FAIL)return -1;++opens;return 17;}
ssize_t dt_test_write(int fd,const void *data,size_t n) {assert(fd==17 && n==8 && *(const uint64_t*)data==DT_BT_HID_EVENT_MASK);return scenario==WRITE_FAIL ? 7 : 8;}
int dt_test_close(int fd) {assert(fd==17 || fd==18);++closes;return 0;}
int dt_test_kqueue(void) {if(scenario==QUEUE_FAIL)return -1;++opens;return 18;}
int dt_test_usleep(unsigned us) {assert(us==20000);now+=us;return 0;}
int dt_test_kevent(int fd,const struct kevent *change,int changes,struct kevent *ready,int count,const struct timespec *timeout) {
 assert(fd==18);
 if(changes) {assert(changes==1 && change->ident==17 && change->filter==EVFILT_READ && change->flags==EV_ADD);setup=1;return scenario==ADD_FAIL ? -1 : 0;}
 assert(setup && count==1 && timeout && timeout->tv_sec==0 && timeout->tv_nsec>0 && timeout->tv_nsec<=1000000000);
 if(scenario==WAIT_FAIL)return -1;
 if(!queued) {now+=(uint64_t)timeout->tv_nsec/1000;return 0;}
 now+=1000;memset(ready,0,sizeof(*ready));ready->ident=17;ready->filter=EVFILT_READ;return 1;
}
ssize_t dt_test_read(int fd,void *out,size_t size) {
 assert(fd==17 && size==256 && queued);
 if(scenario==MALFORMED) {queued=0;return 15;}
 DtBtNativeEvent e[3]={{.type=0x1a,.peer_token=peer},{.type=0x1e,.peer_token=peer},{.type=0x1f,.peer_token=peer}};
 unsigned n=3;
 if(queued==2) {e[0].type=0x19;n=1;live=0;}else if(queued!=3)live=1;
 queued=0;memcpy(out,e,n*sizeof(e[0]));return n*sizeof(e[0]);
}
int dt_test_ioctl(int fd,unsigned long command,...) {
 assert(fd==17);va_list a;va_start(a,command);
 if(command==DT_BT_DISCONNECT) {assert(*va_arg(a,uint64_t*)==peer);++disconnects;live=0;va_end(a);return 0;}
 DtBtNativeReportRequest *r=va_arg(a,DtBtNativeReportRequest*);va_end(a);assert(r->peer==peer);
 if(command==DT_BT_SET_FEATURE) {assert(r->size==5 && !memcmp(r->buffer,"\xf4\x42\x03\0\0",5));++features;return 0;}
 if(command==DT_BT_SET_OUTPUT) {assert(r->size==49 && r->report_id==1 && ((uint8_t*)r->buffer)[10]==2);++outputs;return scenario==OUTPUT_FAIL ? -1 : 0;}
 assert(command==DT_BT_DEQUEUE && r->size==80);uint8_t *p=r->buffer;p[0]=49;p[1]=0x1f;p[2]=1;return 0;
}
static void reset(void) {opens=closes=registrations=unregistrations=cap_restores=profile_restores=live=disconnects=setup=0;outputs=features=0;queued=1;now=1;logs[0]=0;}
int main(void) {
 for(scenario=OK;scenario<=CREATE_RESTORE_FAIL;++scenario) {
  reset();DtTransport t={0};int result=dt_ps4_bluetooth_create(&t);
  if(scenario==CREATE_RESTORE_FAIL) {assert(result==-2 && !t.ops && cap_restores==1);continue;}
  if(scenario>=CAP_FAIL && scenario<=ADD_FAIL) {
   assert(result==-1 && !t.ops && opens==closes && cap_restores==1 && profile_restores==1);continue;
  }
  assert(result==0 && t.ops && t.kind==DT_TRANSPORT_BLUETOOTH);
  assert(dt_ps4_bluetooth_status(&t)==0);
  if(scenario==MALFORMED || scenario==WAIT_FAIL || scenario==CLOCK_FAIL)assert(dt_ps4_bluetooth_service(&t,1)==-1);
  else {
   assert(t.ops->reconnect(t.context)==1);
   DtInputReport report={0};assert(t.ops->read_report(t.context,&report,4)==1 && report.size==49 && report.data[0]==1);
   assert(t.ops->read_report(t.context,&report,4)==0);
   assert(!outputs);now+=150000;
   assert(t.ops->read_report(t.context,&report,4)==0 && outputs==1);
   assert(t.ops->read_report(t.context,&report,4)==0 && outputs==1);
   if(scenario==OK) {
    queued=2;assert(t.ops->read_report(t.context,&report,20)==-1);
    assert(t.ops->reconnect(t.context)==0);assert(opens==2 && registrations==1);
    queued=1;assert(t.ops->reconnect(t.context)==1);assert(t.ops->read_report(t.context,&report,4)==1);
    now+=150000;assert(t.ops->read_report(t.context,&report,4)==0 && outputs==2);
   }
  }
  assert(dt_ps4_bluetooth_status(&t)==(scenario==MALFORMED || scenario==WAIT_FAIL || scenario==CLOCK_FAIL ? -1 : 0));
  assert(dt_ps4_bluetooth_destroy(&t)==(scenario==LINK_STUCK || scenario==UNREGISTER_FAIL || scenario==RESTORE_FAIL ? 1 : 0));
  assert(dt_ps4_bluetooth_status(&t)==-1);
  assert(!t.ops && opens==closes && unregistrations==1 && cap_restores==1 && profile_restores==1);
  assert(now<=5000000);
  assert(!strstr(logs,"112233") && !strstr(logs,"aabbcc"));
 }
 /* A cable handoff may leave an old peer and queued ready/completion events.
  * Those events must not activate the old stream; every fresh link needs F4. */
 scenario=OK;reset();DtTransport t={0};assert(dt_ps4_bluetooth_create(&t)==0);
 for(unsigned cycle=0;cycle<3;++cycle) {
  queued=1;assert(t.ops->reconnect(t.context)==1);
  DtInputReport report={0};assert(t.ops->read_report(t.context,&report,4)==1);
  assert(features==cycle+1 && live);
  queued=3;assert(dt_ps4_bluetooth_quiesce(&t)==0);
  NativeBluetooth *b=t.context;
  assert(!live && b->suspended && !b->reports.peer && !b->reports.latest.size);
  assert(b->delivered==b->reports.valid && features==cycle+1 && disconnects==(int)cycle+1);
  queued=3;assert(dt_ps4_bluetooth_service(&t,1)==0);
  assert(features==cycle+1 && !b->reports.peer);
  assert(t.ops->reconnect(t.context)==0 && !b->suspended);
  assert(opens==2 && registrations==1 && !cap_restores && !profile_restores);
 }
 assert(dt_ps4_bluetooth_destroy(&t)==0);
 scenario=OK;reset();assert(dt_ps4_bluetooth_create(&t)==0);
 assert(t.ops->reconnect(t.context)==1);scenario=LINK_STUCK;
 assert(dt_ps4_bluetooth_quiesce(&t)<0 && dt_ps4_bluetooth_status(&t)<0);
 scenario=OK;live=0;assert(dt_ps4_bluetooth_destroy(&t)==0);
 puts("PASS PS4 Bluetooth transport: bounded waits, reconnect, setup rollback, USB quiesce/rearm with delayed events across three cycles, and fatal stuck link");
}
