#include "../src/platform/ps4/bluetooth_profile_window.c"
#include <assert.h>
#include <stdio.h>
const intptr_t KERNEL_ADDRESS_IMAGE_BASE=0x10000000;
const size_t KERNEL_IMAGE_SIZE=0x1000000;
static uint8_t code[0x30000], original[0x30000];
static unsigned writes, reads, fail_write, fail_read;
static int wrong_fw;
void dt_log(const char *f,...) {(void)f;}
uint32_t kernel_get_fw_version(void) {return wrong_fw ? 0x11000000 : 0x13520001;}
static uint8_t *mapped(intptr_t address,size_t n) {intptr_t off=address-KERNEL_ADDRESS_IMAGE_BASE-0x5b1610;assert(off>=0 && (size_t)off+n<=sizeof(code));return code+off;}
int kernel_copyout(intptr_t a,void *out,size_t n) {if(++reads==fail_read)return -1;memcpy(out,mapped(a,n),n);return 0;}
int kernel_patch(intptr_t a,const void *before,const void *after,uint16_t n) {
 unsigned off=(unsigned)(a-KERNEL_ADDRESS_IMAGE_BASE);
 assert(((off==0x5b1631 || off==0x5b1621 || off==0x5dc899 || off==0x5b1675) && n==4) || (off==0x5b1638 && n==1));
 uint8_t *p=mapped(a,n);assert(!memcmp(p,before,n));++writes;
 if(writes==fail_write) {if(writes<=PATCH_COUNT)memcpy(p,after,1);return -1;}
 memcpy(p,after,n);return 0;
}
static void reset(void) {memset(code,0xcc,sizeof(code));for(unsigned i=0;i<PATCH_COUNT;++i)memcpy(mapped(KERNEL_ADDRESS_IMAGE_BASE+patches[i].offset,patches[i].length),patches[i].before,patches[i].length);memcpy(original,code,sizeof(code));writes=reads=fail_write=fail_read=0;wrong_fw=0;}
int main(void) {
 DtBtNativeRegistration request={0x054c,0x0268};DtBtProfileWindow w;
 for(unsigned failure=0;failure<=PATCH_COUNT;++failure) {
  reset();fail_write=failure;int r=dt_bt_profile_window_begin(&request,&w);assert(r==(failure ? -1 : 0));
  if(!failure)for(unsigned i=0;i<PATCH_COUNT;++i)assert(!memcmp(mapped(KERNEL_ADDRESS_IMAGE_BASE+patches[i].offset,patches[i].length),patches[i].after,patches[i].length));
  assert(dt_bt_profile_window_end(&w)==0 && !memcmp(code,original,sizeof(code)));
 }
 reset();fail_read=PATCH_COUNT+1;assert(dt_bt_profile_window_begin(&request,&w)==-1);assert(!memcmp(code,original,sizeof(code)));
 reset();wrong_fw=1;assert(dt_bt_profile_window_begin(&request,&w)==-1 && !writes);
 reset();code[0x25]=0x90;assert(dt_bt_profile_window_begin(&request,&w)==-1 && !writes);
 reset();request.product=0x05c4;assert(dt_bt_profile_window_begin(&request,&w)==-1 && !writes);request.product=0x0268;
 reset();assert(dt_bt_profile_window_begin(&request,&w)==0);code[0xf]=0x90;assert(dt_bt_profile_window_end(&w)==-1 && code[0xf]==0x90);assert(dt_bt_profile_window_end(&w)==-1);
 reset();assert(dt_bt_profile_window_begin(&request,&w)==0);fail_write=PATCH_COUNT+1;assert(dt_bt_profile_window_end(&w)==-1);
 puts("PASS generic-HID profile window: exact DS3/firmware signatures, operands only, partial rollback, restoration and foreign-change preservation");
}
