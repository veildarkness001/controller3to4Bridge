#include "bluetooth_native_abi.h"
#include "dt/ps4_bluetooth_probe.h"
#include "dt/log.h"
#include <string.h>
#include <sys/ioctl.h>

/* ioctl0x80046209 dispatch ->0x5b5a3b ->0x5b28b0 toggles only scan bit0.
 * Snapshot first; an attempted ioctl is rolled back even if it reports failure.
 * This opens normal native incoming pairing temporarily, not a persistent
 * registration or a change to another controller's pairing record. */
int dt_bt_scan_window_begin(int fd, DtBtScanWindow *w)
{
    if(!w) return -1;
    memset(w,0,sizeof(*w));
    if(dt_ps4_bluetooth_scan_state(&w->original)!=0 || w->original>3) return -1;
    if(w->original&1) { dt_log("BT native incoming window already enabled; preserve original state\n"); return 0; }
    uint32_t enabled=1; w->attempted=1;
    int result=ioctl(fd,UINT32_C(0x80046209),&enabled);
    uint8_t observed=0;
    int checked=dt_ps4_bluetooth_scan_state(&observed)==0 && observed==(w->original|1);
    dt_log("BT native incoming window result=0x%08x scan_flag_verified=%d\n",(uint32_t)result,checked);
    return result==0 && checked ? 0 : -1;
}
int dt_bt_scan_window_end(int fd, DtBtScanWindow *w)
{
    if(!w) return -1;
    if(!w->attempted) return 0;
    uint32_t enabled=w->original&1;
    int result=ioctl(fd,UINT32_C(0x80046209),&enabled);
    uint8_t observed=0;
    int checked=dt_ps4_bluetooth_scan_state(&observed)==0 && observed==w->original;
    dt_log("BT native incoming window restore=0x%08x scan_flag_verified=%d\n",(uint32_t)result,checked);
    if(result!=0 || !checked) return -1;
    w->attempted=0; return 0;
}
