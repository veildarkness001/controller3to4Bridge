#include "../src/platform/ps4/bluetooth_admission.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

const intptr_t KERNEL_ADDRESS_IMAGE_BASE=0x10000000;
const size_t KERNEL_IMAGE_SIZE=0x1000000;
static uint8_t code[0xb0], original[0xb0];
static unsigned writes, read_calls, ioctl_calls, fail_write, fail_read;
static int wrong_fw, native_error, foreign_change;
void dt_log(const char *format, ...) { (void)format; }
uint32_t kernel_get_fw_version(void) { return wrong_fw ? 0x13500001 : 0x13520001; }
static uint8_t *mapped(intptr_t address, size_t size)
{
    intptr_t off=address-KERNEL_ADDRESS_IMAGE_BASE-0x5b6000;
    assert(off>=0 && (size_t)off+size<=sizeof(code)); return code+off;
}
int32_t kernel_copyout(intptr_t address, void *out, size_t size)
{
    if (++read_calls==fail_read) return -1;
    memcpy(out,mapped(address,size),size);return 0;
}
int kernel_patch(intptr_t address, const void *before, const void *after, uint16_t size)
{
    uint8_t *p=mapped(address,size);
    assert(!memcmp(p,before,size));
    /* Only immediates may change, never an instruction opcode or branch. */
    unsigned off=(unsigned)(address-KERNEL_ADDRESS_IMAGE_BASE);
    assert((off==0x5b6019 || off==0x5b6008 || off==0x5b6094 || off==0x5b6086) ? size==4 :
           (off==0x5b602b || off==0x5b609f) && size==1);
    ++writes;
    if (writes==fail_write) {
        if (writes<=3) memcpy(p,after,1); /* Partial failure on application. */
        return -1;
    }
    memcpy(p,after,size);return 0;
}
int dt_test_ioctl(int fd, unsigned long command, ...)
{
    assert(fd==17); ++ioctl_calls;
    const AdmissionPatch *patches=command==DT_BT_REGISTER_DS3 ? register_patches : unregister_patches;
    for (unsigned i=0;i<3;++i) assert(!memcmp(code+patches[i].offset-0x5b6000,patches[i].after,patches[i].length));
    if (foreign_change) code[patches[2].offset-0x5b6000]=0x90;
    return native_error ? (int)UINT32_C(0x802f3701) : 0;
}
static void reset(void)
{
    memset(code,0xcc,sizeof(code));
    for (unsigned i=0;i<3;++i) {
        memcpy(code+register_patches[i].offset-0x5b6000,register_patches[i].before,register_patches[i].length);
        memcpy(code+unregister_patches[i].offset-0x5b6000,unregister_patches[i].before,unregister_patches[i].length);
    }
    memcpy(original,code,sizeof(code));
    writes=read_calls=ioctl_calls=fail_write=fail_read=0;wrong_fw=native_error=foreign_change=0;
}
int main(void)
{
    DtBtNativeRegistration request={0x054c,0x0268}; int restored;
    for (unsigned which=0;which<2;++which) {
        unsigned long command=which ? DT_BT_UNREGISTER_DS3 : DT_BT_REGISTER_DS3;
        for (unsigned failure=0;failure<=3;++failure) {
            reset();fail_write=failure;
            int ret=dt_bt_admitted_ioctl(17,command,&request,&restored);
            assert(ret==(failure ? -1 : 0) && restored);
            assert(ioctl_calls==(failure ? 0 : 1) && !memcmp(code,original,sizeof(code)));
        }
        reset();native_error=1;
        assert(dt_bt_admitted_ioctl(17,command,&request,&restored)==(int)UINT32_C(0x802f3701));
        assert(restored && !memcmp(code,original,sizeof(code)));
        reset();fail_read=4;
        assert(dt_bt_admitted_ioctl(17,command,&request,&restored)==-1 && restored && !ioctl_calls);
        assert(!memcmp(code,original,sizeof(code)));
        reset();wrong_fw=1;
        assert(dt_bt_admitted_ioctl(17,command,&request,&restored)==-1 && !writes);
        reset();code[(which ? unregister_patches : register_patches)[1].offset-0x5b6000]=0x90;
        assert(dt_bt_admitted_ioctl(17,command,&request,&restored)==-1 && !writes && !ioctl_calls);
        reset();fail_write=4;
        assert(dt_bt_admitted_ioctl(17,command,&request,&restored)==0 && !restored);
        reset();foreign_change=1;
        assert(dt_bt_admitted_ioctl(17,command,&request,&restored)==0 && !restored);
        assert(code[(which ? unregister_patches : register_patches)[2].offset-0x5b6000]==0x90);
    }
    reset();request.product=0x05c4;
    assert(dt_bt_admitted_ioctl(17,DT_BT_REGISTER_DS3,&request,&restored)<0 && !writes && !ioctl_calls);
    puts("PASS Bluetooth admission: exact DS3/firmware guards, operand-only window, partial failures, restoration and foreign-change detection");
}
