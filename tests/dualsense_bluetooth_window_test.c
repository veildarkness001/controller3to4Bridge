#include "../src/platform/ps4/dualsense_bluetooth_window.c"
#include <assert.h>
#include <stdio.h>
const intptr_t KERNEL_ADDRESS_IMAGE_BASE=0x10000000;
const size_t KERNEL_IMAGE_SIZE=0x1000000;
static uint8_t code[PATCH_COUNT][11],original[PATCH_COUNT][11];
static unsigned reads,writes,fail_read,fail_write;
static int wrong_fw;
void dt_log(const char *f,...) {(void)f;}
uint32_t kernel_get_fw_version(void) {return wrong_fw?0x14000000:0x13520001;}
static uint8_t *mapped(intptr_t address,size_t size)
{
    for(unsigned i=0;i<PATCH_COUNT;++i) {
        intptr_t off=address-KERNEL_ADDRESS_IMAGE_BASE-patches[i].offset;
        if(off>=0 && (size_t)off+size<=patches[i].length)return code[i]+off;
    }
    assert(!"unexpected kernel access");return NULL;
}
int kernel_copyout(intptr_t a,void *out,size_t n)
{if(++reads==fail_read)return -1;memcpy(out,mapped(a,n),n);return 0;}
int kernel_patch(intptr_t a,const void *before,const void *after,uint16_t n)
{
    assert(n==4);uint8_t *p=mapped(a,n);assert(!memcmp(p,before,n));++writes;
    if(writes==fail_write) {memcpy(p,after,1);return -1;}
    memcpy(p,after,n);return 0;
}
static void reset(void)
{
    memset(code,0,sizeof(code));
    for(unsigned i=0;i<PATCH_COUNT;++i)memcpy(code[i],patches[i].before,patches[i].length);
    memcpy(original,code,sizeof(code));reads=writes=fail_read=fail_write=wrong_fw=0;
}
int main(void)
{
    for(unsigned failure=0;failure<=PATCH_COUNT;++failure) {
        reset();DtDualsenseBtWindow w={0};fail_write=failure;
        assert(dt_dualsense_bt_window_begin(&w)==(failure?-1:0));
        if(!failure) {
            unsigned saved=writes;assert(dt_dualsense_bt_window_begin(&w)==-1 && writes==saved);
            for(unsigned i=0;i<PATCH_COUNT;++i)assert(!memcmp(code[i],patches[i].after,patches[i].length));
        }
        assert(dt_dualsense_bt_window_end(&w)==0 && !memcmp(code,original,sizeof(code)));
    }
    for(unsigned site=0;site<PATCH_COUNT;++site) {
        reset();DtDualsenseBtWindow w={0};code[site][0]^=1;
        assert(dt_dualsense_bt_window_begin(&w)==-1 && !writes);
    }
    reset();DtDualsenseBtWindow w={0};wrong_fw=1;
    assert(dt_dualsense_bt_window_begin(&w)==-1 && !writes);
    reset();w=(DtDualsenseBtWindow){0};fail_read=PATCH_COUNT+1;
    assert(dt_dualsense_bt_window_begin(&w)==-1 && !memcmp(code,original,sizeof(code)));
    reset();w=(DtDualsenseBtWindow){0};assert(dt_dualsense_bt_window_begin(&w)==0);
    code[0][0]=0x90;assert(dt_dualsense_bt_window_end(&w)==-1 && code[0][0]==0x90);
    assert(dt_dualsense_bt_window_begin(&w)==-1 && dt_dualsense_bt_window_end(&w)==-1);
    reset();w=(DtDualsenseBtWindow){0};assert(dt_dualsense_bt_window_begin(&w)==0);
    fail_write=PATCH_COUNT+1;assert(dt_dualsense_bt_window_end(&w)==-1);
    assert(dt_dualsense_bt_window_begin(NULL)==-1 && dt_dualsense_bt_window_end(NULL)==-1);
    puts("PASS DualSense BT window: firmware/instructions, operand writes, partial rollback, foreign changes and restore failures");
}
