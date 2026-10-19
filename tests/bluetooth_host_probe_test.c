#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include "../src/platform/ps4/bluetooth_host_probe.c"
static int scenario, calls, closes;
static char logs[2048];
unsigned kernel_get_fw_version(void) { return scenario==1 ? 0x11000000 : 0x13520001; }
void dt_log(const char *fmt, ...) {
 va_list a; va_start(a,fmt); size_t n=strlen(logs); vsnprintf(logs+n,sizeof(logs)-n,fmt,a); va_end(a);
}
int dt_test_open(const char *path,int flags,...) { assert(!strcmp(path,"/dev/bt") && flags==(O_RDWR|O_NONBLOCK)); return scenario==2 ? -1 : 17; }
int dt_test_ioctl(int fd,unsigned long cmd,...) {
 assert(fd==17 && cmd==0x80186223); ++calls;
 va_list a; va_start(a,cmd); HostQuery *q=va_arg(a,HostQuery*); va_end(a);
 assert(q->key==3 && q->reserved==0 && q->size==8);
 if (scenario==3 || (scenario==4 && calls==2)) return -1;
 const unsigned char data[8]={0x12,0x34,0x56,0x78,0x9a,0xbc,0,0}; memcpy(q->buffer,data,8);
 if (scenario==5 && calls==2) q->buffer[0]++;
 if (scenario==6) memset(q->buffer,0,8);
 if (scenario==7) memset(q->buffer,0xff,8);
 if (scenario==9) q->buffer[7]=1;
 return 0;
}
int dt_test_close(int fd) { assert(fd==17); ++closes; return scenario==8 ? -1 : 0; }
int main(void) {
 for(scenario=0;scenario<=9;++scenario) {
  calls=closes=0; logs[0]=0;
  int ret=dt_ps4_bluetooth_host_probe(); assert(ret==(scenario==0 ? 0 : scenario==8 ? 2 : 1));
  assert(closes==(scenario>=2 ? scenario!=2 : scenario==0));
  assert(calls==(scenario==1 || scenario==2 ? 0 : scenario==3 ? 1 : 2));
  assert(!strstr(logs,"123456") && !strstr(logs,"12:34"));
 }
 puts("PASS host property: exact read-only ABI, firmware guard, consistency, failure cleanup, value redaction");
}
