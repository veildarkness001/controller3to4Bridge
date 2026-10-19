#include "dt/ps4_bluetooth_probe.h"
#include "dt/log.h"
#include "bluetooth_native_abi.h"
#include <ps4/kernel.h>
#include <string.h>
#include <unistd.h>

/* 13.52 /dev/bt write0x5b580e..0x5b5839 keeps only HID events unless
 * predicate0x3b2ce0 sees bit62 of ucred capabilities. Enable that one bit
 * temporarily, keep authid unchanged, then restore and verify all16bytes. */
int dt_bt_event_window_begin(DtBtEventWindow *w)
{
    if(!w) return 1;
    memset(w,0,sizeof(*w));
    if((kernel_get_fw_version()&0xffff0000u)!=0x13520000u ||
       !KERNEL_ADDRESS_IMAGE_BASE || KERNEL_IMAGE_SIZE<0x3b2cef) return 1;
    const uint8_t expected[]={0x48,0x8b,0x47,0x60,0x48,0xc1,0xe8,0x3e,0x83,0xe0,1};
    uint8_t code[sizeof(expected)];
    if(kernel_copyout(KERNEL_ADDRESS_IMAGE_BASE+0x3b2ce4,code,sizeof(code))!=0 ||
       memcmp(code,expected,sizeof(code))) return 1;
    w->pid=getpid(); uint8_t desired[16], observed[16];
    if(kernel_get_ucred_caps(w->pid,w->saved)!=0) return 1;
    memcpy(desired,w->saved,16); desired[7]|=0x40;
    w->attempted=1;
    int set=kernel_set_ucred_caps(w->pid,desired);
    int verified=set==0 && kernel_get_ucred_caps(w->pid,observed)==0 && !memcmp(desired,observed,16);
    dt_log("BT event capability bit62 applied=%d verified=%d; authid unchanged\n",set==0,verified);
    memset(desired,0,16); memset(observed,0,16);
    return verified ? 0 : 1;
}
int dt_bt_event_window_end(DtBtEventWindow *w)
{
    if(!w) return -1;
    if(!w->attempted) return w->failed_restore ? -1 : 0;
    uint8_t observed[16];
    int restored=kernel_set_ucred_caps(w->pid,w->saved);
    int checked=restored==0 && kernel_get_ucred_caps(w->pid,observed)==0 && !memcmp(w->saved,observed,16);
    dt_log("BT event original capabilities restored=%d verified=%d\n",restored==0,checked);
    w->attempted=0;w->failed_restore=!checked;
    memset(w->saved,0,16);memset(observed,0,16);
    return checked ? 0 : -1;
}
#ifndef DT_BT_CAPABILITIES_ONLY
int dt_ps4_bluetooth_events_probe(void)
{
    DtBtEventWindow window={0};
    int status=dt_bt_event_window_begin(&window)==0 ? dt_ps4_bluetooth_native_probe() : 1;
    return dt_bt_event_window_end(&window)==0 ? status : 3;
}
#endif
