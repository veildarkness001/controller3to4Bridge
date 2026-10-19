#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../src/platform/ps4/bluetooth_event_privileges.c"
const intptr_t KERNEL_ADDRESS_IMAGE_BASE=0x10000000;
const size_t KERNEL_IMAGE_SIZE=0x4000000;
static uint8_t caps[16], original[16];
static int scenario, sets, cap_reads, probes;
unsigned kernel_get_fw_version(void) {return scenario==1 ? 0x11000000 : 0x13520001;}
int kernel_copyout(intptr_t a,void *out,size_t n) {assert(a==KERNEL_ADDRESS_IMAGE_BASE+0x3b2ce4 && n==11);const uint8_t v[11]={0x48,0x8b,0x47,0x60,0x48,0xc1,0xe8,0x3e,0x83,0xe0,1};memcpy(out,v,n);if(scenario==2)((uint8_t*)out)[0]^=1;return 0;}
int kernel_get_ucred_caps(pid_t p,uint8_t *out) {assert(p==getpid());++cap_reads;if(scenario==3 && cap_reads==1)return -1;memcpy(out,caps,16);if(scenario==5 && cap_reads==2)out[0]^=1;return 0;}
int kernel_set_ucred_caps(pid_t p,const uint8_t *in) {
 assert(p==getpid()); ++sets;
 if(sets==1) { for(unsigned i=0;i<16;++i) assert(in[i]==(uint8_t)(original[i]|(i==7 ? 0x40 : 0))); }
 else assert(!memcmp(in,original,16));
 memcpy(caps,in,16);
 return (scenario==4 && sets==1) || (scenario==6 && sets==2) ? -1 : 0;
}
int dt_ps4_bluetooth_native_probe(void) {++probes;assert(caps[7]&0x40);return scenario==7 ? 2 : 0;}
void dt_log(const char *f,...) {(void)f;}
int main(void) {
 for(scenario=0;scenario<=7;++scenario) {
  for(unsigned i=0;i<16;++i)original[i]=caps[i]=i;
  sets=cap_reads=probes=0;int r=dt_ps4_bluetooth_events_probe();
  assert(r==(scenario==0 ? 0 : scenario==6 ? 3 : scenario==7 ? 2 : 1));
  assert(!memcmp(caps,original,16));
  assert(probes==(scenario==0 || scenario==6 || scenario==7));
  assert(sets==(scenario==1 || scenario==2 || scenario==3 ? 0 : 2));
 }
 puts("PASS minimal event capability: signature guard, one bit only, partial failures, native result, snapshot restoration and verification");
}
