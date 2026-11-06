#include "dt/ps4_dualsense_bluetooth.h"
#include "dt/log.h"
#include <ps4/kernel.h>
#include <stdint.h>
#include <string.h>

/* Candidate sites identified from OneFamily acacf0d (MIT; see THIRD_PARTY),
 * then independently read on 13.52 and checked against local disassembly.
 * Substitute only the 044f:1000 generic-HID rule with Sony 054c:0ce6. The
 * DS3 window substitutes a different rule, but overlapping Sony classification
 * means these listeners cannot be enabled concurrently. No AOB guessing. */
typedef struct { uint32_t offset; uint8_t length, operand, before[11], after[11]; } BtPatch;
#define EAX_SITE(off,lo,hi) {off,5,1,{0x3d,lo,hi,0,0},{0x3d,0x4c,0x05,0,0}}
#define EAX_PID(off) {off,5,1,{0x3d,0,0x10,0,0},{0x3d,0xe6,0x0c,0,0}}
static const BtPatch patches[]={
    EAX_SITE(0x59c868,0x4f,0x04), EAX_PID(0x59c8bf),
    {0x5b1614,6,2,{0x81,0xf9,0x4f,4,0,0},{0x81,0xf9,0x4c,5,0,0}},
    {0x5b1644,6,2,{0x81,0xf9,0,0x10,0,0},{0x81,0xf9,0xe6,0x0c,0,0}},
    {0x5b1e84,6,2,{0x81,0xfe,0x4f,4,0,0},{0x81,0xfe,0x4c,5,0,0}},
    {0x5b1eb7,6,2,{0x81,0xfe,0,0x10,0,0},{0x81,0xfe,0xe6,0x0c,0,0}},
    EAX_SITE(0x5b5c47,0x4f,0x04),
    {0x5b64dc,6,2,{0x81,0xf9,0,0x10,0,0},{0x81,0xf9,0xe6,0x0c,0,0}},
    EAX_SITE(0x5b5ffc,0x4f,0x04), EAX_PID(0x5b656d),
    EAX_SITE(0x5b607a,0x4f,0x04),
    {0x5b3488,6,2,{0x81,0xf9,0,0x10,0,0},{0x81,0xf9,0xe6,0x0c,0,0}},
    /* Existing DS3 investigation proved capacity22 here, native buffer128.
     * Full DualSense report78 fits; firmware signatures remain mandatory. */
    {0x5b166e,11,7,{0x41,0xc7,0x86,0x74,0x15,0,0,0x16,0,0,0},
                  {0x41,0xc7,0x86,0x74,0x15,0,0,0x4e,0,0,0}},
};
#define PATCH_COUNT (sizeof(patches)/sizeof(patches[0]))
static int read_site(const BtPatch *p,uint8_t *out)
{ return kernel_copyout(KERNEL_ADDRESS_IMAGE_BASE+p->offset,out,p->length); }
int dt_dualsense_bt_window_end(DtDualsenseBtWindow *w)
{
    if(!w || w->attempted>PATCH_COUNT)return -1;
    while(w->attempted) {
        const BtPatch *p=&patches[--w->attempted];uint8_t current[11];
        if(read_site(p,current)!=0) {w->failed_restore=1;continue;}
        int owned=1;
        for(unsigned j=0;j<p->length;++j)
            if(current[j]!=p->before[j] &&
               (j<p->operand || j>=p->operand+4 || current[j]!=p->after[j]))owned=0;
        if(!owned) {w->failed_restore=1;continue;}
        if(memcmp(current,p->before,p->length) &&
           kernel_patch(KERNEL_ADDRESS_IMAGE_BASE+p->offset+p->operand,
                        current+p->operand,p->before+p->operand,4)!=0)w->failed_restore=1;
        if(read_site(p,current)!=0 || memcmp(current,p->before,p->length))w->failed_restore=1;
    }
    dt_log("DualSense BT original instructions restored=%d\n",!w->failed_restore);
    return w->failed_restore?-1:0;
}
int dt_dualsense_bt_window_begin(DtDualsenseBtWindow *w)
{
    if(!w || w->attempted || w->failed_restore ||
       (kernel_get_fw_version()&0xffff0000u)!=0x13520000u ||
       !KERNEL_ADDRESS_IMAGE_BASE || KERNEL_IMAGE_SIZE<0x5b6574)return -1;
    uint8_t current[11];
    for(unsigned i=0;i<PATCH_COUNT;++i)
        if(read_site(&patches[i],current)!=0 || memcmp(current,patches[i].before,patches[i].length)) {
            dt_log("DualSense BT instruction mismatch; no writes\n");return -1;
        }
    for(unsigned i=0;i<PATCH_COUNT;++i) {
        const BtPatch *p=&patches[i];w->attempted=i+1;
        if(kernel_patch(KERNEL_ADDRESS_IMAGE_BASE+p->offset+p->operand,
                        p->before+p->operand,p->after+p->operand,4)!=0 ||
           read_site(p,current)!=0 || memcmp(current,p->after,p->length))
            return dt_dualsense_bt_window_end(w)==0?-1:-2;
    }
    dt_log("DualSense BT classification window enabled; firmware13.52 only\n");
    return 0;
}
