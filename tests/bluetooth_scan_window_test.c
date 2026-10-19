#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "../src/platform/ps4/bluetooth_scan_window.c"
static int scenario, calls, reads;
static uint8_t scan;
int dt_ps4_bluetooth_scan_state(uint8_t *out) {++reads;*out=scan;if(scenario==1 && reads==1)return -1;if(scenario==3 && reads==2)*out=0;return 0;}
int dt_test_ioctl(int fd,unsigned long command,...) {
 assert(fd==17 && command==0x80046209);++calls;
 va_list a;va_start(a,command);uint32_t *value=va_arg(a,uint32_t*);va_end(a);
 assert(*value==(calls==1 ? 1u : 0u));scan=(scan&~1u)|*value;
 return (scenario==2 && calls==1) || (scenario==4 && calls==2) ? -1 : 0;
}
void dt_log(const char *f,...) {(void)f;}
int main(void) {
 for(scenario=0;scenario<=6;++scenario) {
  scan=scenario==6 ? 3 : scenario==5 ? 4 : 2;calls=reads=0;
  DtBtScanWindow w={0};int result=dt_bt_scan_window_begin(17,&w);
  assert(result==(scenario==0 || scenario==4 || scenario==6 ? 0 : -1));
  int end=dt_bt_scan_window_end(17,&w);assert(end==(scenario==4 ? -1 : 0));
  assert(scan==(scenario==6 ? 3 : scenario==5 ? 4 : 2));
  assert(calls==(scenario==1 || scenario==5 || scenario==6 ? 0 : 2));
 }
 puts("PASS incoming window: preserve existing mode, exact ioctl, failure rollback, readback and restoration");
}
