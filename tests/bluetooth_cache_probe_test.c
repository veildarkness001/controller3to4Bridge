#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../src/platform/ps4/bluetooth_cache_probe.c"
const intptr_t KERNEL_ADDRESS_IMAGE_BASE=0x10000000;
const size_t KERNEL_IMAGE_SIZE=0x3000000;
static const uint64_t context=UINT64_C(0xffff900000001000);
static int scenario, reads, closes, copies, matches;
static int own_token_reads, channel_reads;
static char logs[2048];
unsigned kernel_get_fw_version(void) { return scenario==1 ? 0x11000000 : 0x13520001; }
int kernel_copyout(intptr_t address,void *out,size_t n) {
 ++copies; memset(out,0,n);
 if(scenario==5 && copies==2) return -1;
 if(address==KERNEL_ADDRESS_IMAGE_BASE+0x5b4a70) { const uint8_t signature[7]={0x48,0x8d,0x05,0xd9,0xda,0x05,0x02};assert(n==7);memcpy(out,signature,n);if(scenario==2)((uint8_t*)out)[0]^=1; }
 else if(address==KERNEL_ADDRESS_IMAGE_BASE+0x2612550) { assert(n==8);uint64_t c=scenario==6 ? 1 : context;memcpy(out,&c,8); }
 else if((uint64_t)address==context || (uint64_t)address==context+8 || (uint64_t)address==context+0x22b48) {}
 else if((uint64_t)address>=context+0x42a8 && (uint64_t)address<context+0xd960) {
  uint64_t relative=(uint64_t)address-context-0x42a8; unsigned slot=relative/0x1588; unsigned field=relative%0x1588;
  if(field==0x30) {
   assert(n==8);uint64_t token=UINT64_C(0xaabbccddeeff);
   if(slot==2 && scenario!=7) {token=UINT64_C(0x112233445566);if(scenario==9 && own_token_reads++)token=0;}
   memcpy(out,&token,8);
  } else {
   assert(slot==2); /* Never read foreign connection fields. */
   if(scenario==10)return -1;
   if(field>=0x650 && field<0xad0) {
    ++channel_reads; unsigned f=(field-0x650)%0x90;
    assert((f==0x3c && n==2) || (f==8 && n==4));
    if(field==0x650+0x3c) {uint16_t psm=0x11;memcpy(out,&psm,2);}
    if(field==0x650+8) {uint32_t flags=0x98;memcpy(out,&flags,4);}
   } else {assert(n==4 && (field==0x18 || field==0x12d0 || field==0x12e4 || field==0x12e8));}
  }
 }
 else if((uint64_t)address>=context+0x4b6b8 && (uint64_t)address<context+0x4b7b8) {
  assert(n==8); unsigned i=((uint64_t)address-context-0x4b6b8)/8;
  uint64_t token=i==2 && scenario!=7 ? UINT64_C(0x112233445566) : UINT64_C(0xaabbccddeeff); memcpy(out,&token,8);
 }

 else {
  assert((uint64_t)address>=context+0x18da8 && (uint64_t)address<context+0x20ea8 && n==0x30);
  unsigned i=((uint64_t)address-context-0x18da8)/0x408;
  if(i==4 && scenario!=7) { uint64_t token=UINT64_C(0x112233445566);memcpy(out,&token,8);uint16_t vid=0x054c,pid=0x0268;memcpy((uint8_t*)out+0x28,&vid,2);memcpy((uint8_t*)out+0x2a,&pid,2);++matches; }
  else { uint64_t foreign=UINT64_C(0xaabbccddeeff);memcpy(out,&foreign,8); }
  if(scenario==8 && i==6) return -1;
 }
 return 0;
}
int dt_test_open(const char *p,int f,...) { assert(!strcmp(p,"/data/dualshock_to_ps4/ds3-pairing-backup.bin") && f==O_RDONLY);return scenario==3 ? -1 : 17; }
ssize_t dt_test_read(int fd,void *out,size_t n) { assert(fd==17);if(reads++)return 0;assert(n==20);memset(out,0,n);memcpy(out,"DTPAIR1",8);const uint8_t id[6]={0x11,0x22,0x33,0x44,0x55,0x66};memcpy((uint8_t*)out+8,id,6);return scenario==4 ? 19 : 20; }
int dt_test_close(int fd) {assert(fd==17);++closes;return 0;}
void dt_log(const char *f,...) {va_list a;va_start(a,f);size_t n=strlen(logs);vsnprintf(logs+n,sizeof(logs)-n,f,a);va_end(a);}
int main(void) {
 for(scenario=0;scenario<=8;++scenario) {
  reads=closes=copies=matches=0;logs[0]=0;
  int ret=dt_ps4_bluetooth_cache_probe(); assert(ret==(scenario==0 || scenario==7 ? 0 : 1));
  assert(closes==(scenario==0 || scenario==4 || scenario==7 || scenario==8));
  assert(!strstr(logs,"112233") && !strstr(logs,"aabbcc") && !strstr(logs,"ffff9000"));
  if(scenario==0) assert(strstr(logs,"matching_ds3=1") && strstr(logs,"vendor=0x054c product=0x0268"));
  if(scenario==7) assert(strstr(logs,"matching_ds3=0"));
#ifdef DT_BT_CACHE_REJECTIONS
  if(scenario==0) assert(strstr(logs,"prior_notification_matches_ds3=1"));
  if(scenario==7) assert(strstr(logs,"prior_notification_matches_ds3=0"));
#endif
 }
 for(scenario=0;scenario<=10;++scenario) {
  if(scenario!=0 && scenario!=7 && scenario!=9 && scenario!=10)continue;
  own_token_reads=0;
  assert(dt_ps4_bluetooth_link_present(UINT64_C(0x112233445566))==(scenario==7 ? 0 : 1));
  logs[0]=0;own_token_reads=channel_reads=0;
  assert(dt_ps4_bluetooth_link_probe(UINT64_C(0x112233445566))==(scenario>=9));
  assert(!strstr(logs,"112233") && !strstr(logs,"aabbcc") && !strstr(logs,"ffff9000"));
  if(scenario==0)assert(strstr(logs,"channel psm=0x11 flags=0x98") && channel_reads==16);
  if(scenario==7)assert(strstr(logs,"absent") && channel_reads==0);
  if(scenario>=9)assert(!logs[0]);
 }
 assert(dt_ps4_bluetooth_link_probe(0)==1);
 assert(dt_ps4_bluetooth_link_present(0)==-1);
 assert(dt_ps4_bluetooth_link_probe(UINT64_C(0x1000000000000))==1);
 puts("PASS read-only known-DS3 cache: firmware/signature/pointer guards, failures, matching-only metadata and identity redaction");
}
