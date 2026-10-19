#include "bluetooth_native_abi.h"
#include "dt/log.h"
#include <ps4/kernel.h>
#include <string.h>
#include <sys/ioctl.h>

/* Experimental 13.52 admission window, not permanent Bluetooth support.
 * Dispatch 0xad5cb8 proves REGISTER -> 0x5b5fb6, UNREGISTER -> 0x5b6037.
 * Temporarily substitute the 06f8:e051/e052 rule with 054c:0268 ONLY while
 * issuing our registration ioctl, then restore it before returning. The 044f
 * rule and all opcodes/branches remain intact. Concurrent registration of the
 * replaced 06f8 devices may fail during that short window; this is not a
 * production coexistence solution. No patch is held while listening for input.
 * Evidence: bt-0.3.1-code-detail-first and bt-0.3.2-dispatch-table-first. */
typedef struct {
    uint32_t offset;
    uint8_t length, operand, size;
    uint8_t before[8], after[8];
} AdmissionPatch;

static const AdmissionPatch register_patches[] = {
    {0x5b6015,8,4,4,{0x41,0x8d,0x84,0x24,0xaf,0x1f,0,0},
                    {0x41,0x8d,0x84,0x24,0x98,0xfd,0xff,0xff}}, /* PID -0268 */
    {0x5b6028,4,3,1,{0x66,0x83,0xf8,2},{0x66,0x83,0xf8,1}}, /* one PID */
    {0x5b6007,5,1,4,{0x3d,0xf8,6,0,0},{0x3d,0x4c,5,0,0}}, /* Sony */
};
static const AdmissionPatch unregister_patches[] = {
    {0x5b6090,8,4,4,{0x41,0x8d,0x84,0x24,0xaf,0x1f,0,0},
                    {0x41,0x8d,0x84,0x24,0x98,0xfd,0xff,0xff}},
    {0x5b609c,4,3,1,{0x66,0x83,0xf8,2},{0x66,0x83,0xf8,1}},
    {0x5b6085,5,1,4,{0x3d,0xf8,6,0,0},{0x3d,0x4c,5,0,0}},
};

static int read_instruction(const AdmissionPatch *p, uint8_t *bytes)
{
    return kernel_copyout(KERNEL_ADDRESS_IMAGE_BASE+p->offset,bytes,p->length);
}

int dt_bt_admitted_ioctl(int fd, unsigned long command,
                         DtBtNativeRegistration *request, int *restored)
{
    if (!restored) return -1;
    *restored=1;
    if (!request || request->vendor!=0x054c || request->product!=0x0268 ||
        (kernel_get_fw_version() & 0xffff0000u)!=0x13520000u ||
        !KERNEL_ADDRESS_IMAGE_BASE || KERNEL_IMAGE_SIZE<0x5b60a0) return -1;
    const AdmissionPatch *patches;
    if (command==DT_BT_REGISTER_DS3) patches=register_patches;
    else if (command==DT_BT_UNREGISTER_DS3) patches=unregister_patches;
    else return -1;
    uint8_t current[8];
    /* Preflight all instructions before making the first write. Never adopt
     * a pre-existing patch as our own, including a byte-identical one. */
    for (unsigned i=0;i<3;++i)
        if (read_instruction(&patches[i],current)!=0 ||
            memcmp(current,patches[i].before,patches[i].length)) {
            dt_log("BT admission signature mismatch; no writes\n"); return -1;
        }
    int result=-1;
    unsigned attempted=0;
    for (unsigned i=0;i<3;++i) {
        const AdmissionPatch *p=&patches[i];
        attempted=i+1; /* A failed copy can be partial: include it in rollback. */
        if (kernel_patch(KERNEL_ADDRESS_IMAGE_BASE+p->offset+p->operand,
                         p->before+p->operand,p->after+p->operand,p->size)!=0 ||
            read_instruction(p,current)!=0 || memcmp(current,p->after,p->length)) goto rollback;
    }
    result=ioctl(fd,command,request);
rollback:
    while (attempted) {
        const AdmissionPatch *p=&patches[--attempted];
        if (read_instruction(p,current)!=0) { *restored=0; continue; }
        int owned=1;
        for (unsigned j=0;j<p->length;++j)
            if (current[j]!=p->before[j] &&
                (j<p->operand || j>=p->operand+p->size || current[j]!=p->after[j])) owned=0;
        if (!owned) { *restored=0; continue; } /* Do not clobber a foreign change. */
        if (memcmp(current,p->before,p->length) &&
            kernel_patch(KERNEL_ADDRESS_IMAGE_BASE+p->offset+p->operand,
                         current+p->operand,p->before+p->operand,p->size)!=0) *restored=0;
        if (read_instruction(p,current)!=0 || memcmp(current,p->before,p->length)) *restored=0;
    }
    dt_log("BT admission original instructions restored=%d\n",*restored);
    return result;
}
