#include "bluetooth_native_abi.h"
#include "dt/bluetooth.h"
#include "dt/log.h"
#include <string.h>
#include <sys/ioctl.h>

void dt_bt_report_probe_event(int fd,uint64_t known,const DtBtNativeEvent *e,DtBtReportProbe *p)
{
    if(!p || !e || !known || (known>>48) ||
       (e->peer_token&UINT64_C(0xffffffffffff))!=known) return;
    if(e->type==0x1a && !e->status && p->peer!=e->peer_token) {
        p->peer=e->peer_token;p->accepted=p->completed=0;++p->attempts;
        p->latest.size=0;
        uint8_t report[5];
        size_t size=dt_ds3_bluetooth_enable(report,sizeof(report),DT_BT_HID_REPORT);
        if(size!=5) {++p->errors;return;}
        DtBtNativeReportRequest request={.peer=e->peer_token,.buffer=report,.size=size,.report_id=0xf4};
        int result=ioctl(fd,DT_BT_SET_FEATURE,&request);
        p->accepted=result==0;
        if(!p->accepted)++p->errors;
        dt_log("BT DS3 F4 submitted result=0x%08x; completion pending\n",(uint32_t)result);
    } else if(e->peer_token==p->peer && e->type==0x1e && p->accepted) {
        ++p->completions;p->completed=e->status==0;
        if(!p->completed)++p->errors;
        dt_log("BT DS3 F4 completion status=0x%02x success=%d\n",e->status,p->completed);
    } else if(e->peer_token==p->peer && e->type==0x1f && p->accepted) {
        uint8_t packet[80]={0};
        DtBtNativeReportRequest request={.peer=e->peer_token,.buffer=packet,.size=sizeof(packet)};
        int result=ioctl(fd,DT_BT_DEQUEUE,&request);
        if(result<0) {++p->errors;return;}
        /* RX0x5b51b9..0x5b51e9: byte0=payload length, byte1=event type.
         * The kernel strips HIDP0xa1; the report ID remains in payload[0]. */
        size_t size=packet[0];
        DtInputReport report;DtControllerState state;
        if(packet[1]!=0x1f || size>sizeof(packet)-2 || size!=49 ||
           dt_ds3_bluetooth_unpack(packet+2,size,DT_BT_HID_REPORT,dt_now_us(),&report)!=0 ||
           dt_ds3_decode(&report,&state)!=0) {++p->invalid;return;}
        if(p->valid && (state.buttons!=p->last.buttons || state.lx!=p->last.lx || state.ly!=p->last.ly ||
           state.rx!=p->last.rx || state.ry!=p->last.ry || state.l2!=p->last.l2 || state.r2!=p->last.r2))++p->changes;
        p->last=state;p->latest=report;++p->valid;
        if(p->valid==1)dt_log("BT DS3 first valid radio report bytes=%zu\n",size);
    } else if(e->peer_token==p->peer && (e->type==6 || e->type==0x19)) {
        p->peer=0;p->accepted=p->completed=0;
        p->latest.size=0;
    }
}
