#include "bluetooth_native_abi.h"
#include "dt/log.h"
#include <ps4/kernel.h>
#include <string.h>

/* Experimental generic-HID classification on13.52: callback0x5b1606 checks
 * the same044f/06f8 whitelist as registration before assigning a peer to our
 * registered client. Substitute06f8:e051/e052 with054c:0268 during this test;
 * preserve044f and all opcodes/branches. This can prevent06f8 device admission
 * during the bounded window, and is not a production coexistence solution. */
typedef struct { uint32_t offset; uint8_t length, operand, size, before[12], after[12]; } ProfilePatch;
static const ProfilePatch patches[]={
#ifdef DT_BT_IGNORE_NATIVE_DS3
    /* Native input manager0x5dc790 blacklists exactly054c:0268 and calls
     * disconnect at0x5dc9b6. Retarget that DS3-only branch to its epilogue,
     * leaving0306 and old DS4 rejection unchanged. Our registered HID client
     * owns the experimental DS3; the native input manager must ignore it. */
    {0x5dc897,6,2,4,{0x0f,0x85,0x19,0x01,0,0},{0x0f,0x85,0xbd,0x02,0,0}},
#endif
#ifdef DT_BT_ACTIVATE_REPORTS
    /* Generic fallback capacity22 rejects the DS3 report49 before queueing.
     * This bounded test also raises044f:1000 fallback capacity to49; it is
     * not a production coexistence design. No buffer exceeds native128. */
    {0x5b166e,11,7,4,{0x41,0xc7,0x86,0x74,0x15,0,0,0x16,0,0,0},{0x41,0xc7,0x86,0x74,0x15,0,0,0x31,0,0,0}},
#endif
    {0x5b162f,6,2,4,{0x81,0xc1,0xaf,0x1f,0,0},{0x81,0xc1,0x98,0xfd,0xff,0xff}},
    {0x5b1635,4,3,1,{0x66,0x83,0xf9,2},{0x66,0x83,0xf9,1}},
    {0x5b161f,6,2,4,{0x81,0xf9,0xf8,6,0,0},{0x81,0xf9,0x4c,5,0,0}},
};
#define PATCH_COUNT (sizeof(patches)/sizeof(patches[0]))
static int instruction(const ProfilePatch *p,uint8_t out[12])
{ return kernel_copyout(KERNEL_ADDRESS_IMAGE_BASE+p->offset,out,p->length); }
int dt_bt_profile_window_end(DtBtProfileWindow *w)
{
    if(!w) return -1;
    while(w->attempted) {
        const ProfilePatch *p=&patches[--w->attempted]; uint8_t current[12];
        if(instruction(p,current)!=0) { w->failed_restore=1; continue; }
        int owned=1;
        for(unsigned i=0;i<p->length;++i)
            if(current[i]!=p->before[i] &&
               (i<p->operand || i>=p->operand+p->size || current[i]!=p->after[i])) owned=0;
        if(!owned) { w->failed_restore=1; continue; }
        if(memcmp(current,p->before,p->length) && kernel_patch(KERNEL_ADDRESS_IMAGE_BASE+p->offset+p->operand,
            current+p->operand,p->before+p->operand,p->size)!=0) w->failed_restore=1;
        if(instruction(p,current)!=0 || memcmp(current,p->before,p->length)) w->failed_restore=1;
    }
    dt_log("BT generic profile original instructions restored=%d\n",!w->failed_restore);
    return w->failed_restore ? -1 : 0;
}
int dt_bt_profile_window_begin(const DtBtNativeRegistration *request,DtBtProfileWindow *w)
{
    if(!w) return -1;
    memset(w,0,sizeof(*w));
    if(!request || request->vendor!=0x054c || request->product!=0x0268 ||
       (kernel_get_fw_version()&0xffff0000u)!=0x13520000u ||
       !KERNEL_ADDRESS_IMAGE_BASE || KERNEL_IMAGE_SIZE<0x5dc89d) return -1;
    uint8_t current[12];
    for(unsigned i=0;i<PATCH_COUNT;++i)
        if(instruction(&patches[i],current)!=0 || memcmp(current,patches[i].before,patches[i].length)) return -1;
    for(unsigned i=0;i<PATCH_COUNT;++i) {
        const ProfilePatch *p=&patches[i]; w->attempted=i+1;
        if(kernel_patch(KERNEL_ADDRESS_IMAGE_BASE+p->offset+p->operand,p->before+p->operand,p->after+p->operand,p->size)!=0 ||
           instruction(p,current)!=0 || memcmp(current,p->after,p->length))
            return dt_bt_profile_window_end(w)==0 ? -1 : -2;
    }
    dt_log("BT generic profile DS3 window enabled; bounded experiment\n"); return 0;
}
