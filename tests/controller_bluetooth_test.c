#include "dt/controller_bluetooth.h"
#include "dt/ps4_bluetooth.h"
#include "dt/ps4_dualsense_bluetooth.h"
#include <assert.h>
#include <stdio.h>
static const DtTransportOps ds3_ops={0},ds5_ops={0};
static unsigned ds3_calls,ds5_calls;
static int fault;
static int automatic,live,target,unavailable=-1,rollback=-1,failed_destroy,wrong_format;
static unsigned opens[2],closes[2];
static uint64_t now=1,ready_at;
uint64_t dt_now_us(void) {return now;}
void dt_log(const char *fmt,...) {(void)fmt;}
static int ready(void *context)
{int model=(int)(intptr_t)context;assert(live==1);return model==target && now>=ready_at;}
static int report(void *context,DtInputReport *out,unsigned timeout)
{
    assert(timeout && timeout<=1000);int model=(int)(intptr_t)context;
    if(!ready(context))return 0;
    *out=(DtInputReport){.size=model?10:49,.timestamp_us=now,
        .format=wrong_format?DT_REPORT_DUALSENSE_USB:model?DT_REPORT_DUALSENSE_BT:DT_REPORT_DS3};
    out->data[0]=1;return 1;
}
static const DtTransportOps auto_ds3={.reconnect=ready,.read_report=report},auto_ds5={.reconnect=ready,.read_report=report};
static int create_auto(DtTransport *t,int model)
{
    ++opens[model];assert(!live);
    if(model==rollback)return -2;
    if(model==unavailable)return -1;
    ++live;*t=(DtTransport){.kind=DT_TRANSPORT_BLUETOOTH,.ops=model?&auto_ds5:&auto_ds3,.context=(void*)(intptr_t)model};return 0;
}
int dt_ps4_bluetooth_create(DtTransport *t)
{if(automatic)return create_auto(t,0);++ds3_calls;*t=(DtTransport){.kind=DT_TRANSPORT_BLUETOOTH,.ops=&ds3_ops};return 0;}
int dt_ps4_dualsense_bluetooth_create(DtTransport *t)
{if(automatic)return create_auto(t,1);++ds5_calls;*t=(DtTransport){.kind=DT_TRANSPORT_BLUETOOTH,.ops=&ds5_ops};return 0;}
int dt_ps4_dualsense_bluetooth_matches(const DtTransport *t) {return t && (t->ops==&ds5_ops || t->ops==&auto_ds5);}
int dt_ps4_bluetooth_status(const DtTransport *t) {assert(t->ops==&ds3_ops || t->ops==&auto_ds3);++ds3_calls;return fault?-1:0;}
int dt_ps4_dualsense_bluetooth_status(const DtTransport *t) {assert(t->ops==&ds5_ops || t->ops==&auto_ds5);++ds5_calls;return fault?-1:0;}
int dt_ps4_bluetooth_quiesce(DtTransport *t) {assert(t->ops==&ds3_ops);++ds3_calls;return fault?-1:0;}
int dt_ps4_dualsense_bluetooth_quiesce(DtTransport *t) {assert(t->ops==&ds5_ops);++ds5_calls;return fault?-1:0;}
static int destroy_auto(DtTransport *t,int model)
{assert(live==1 && t->ops==(model?&auto_ds5:&auto_ds3));--live;++closes[model];*t=(DtTransport){0};return failed_destroy;}
int dt_ps4_bluetooth_destroy(DtTransport *t) {if(automatic)return destroy_auto(t,0);assert(t->ops==&ds3_ops);++ds3_calls;*t=(DtTransport){0};return fault?1:0;}
int dt_ps4_dualsense_bluetooth_destroy(DtTransport *t) {if(automatic)return destroy_auto(t,1);assert(t->ops==&ds5_ops);++ds5_calls;*t=(DtTransport){0};return fault?1:0;}
int main(void)
{
    for(unsigned model=0;model<2;++model)for(fault=0;fault<2;++fault) {
        DtTransport t={0};ds3_calls=ds5_calls=0;
        assert(dt_controller_bluetooth_create(&t,(DtBluetoothController)model)==0);
        assert(dt_controller_bluetooth_create(&t,(DtBluetoothController)model)==-1);
        assert(dt_controller_bluetooth_status(&t)==(fault?-1:0));
        assert(dt_controller_bluetooth_quiesce(&t)==(fault?-1:0));
        assert(dt_controller_bluetooth_destroy(&t)==(fault?1:0) && !t.ops);
        assert((model?ds5_calls:ds3_calls)==4 && (model?ds3_calls:ds5_calls)==0);
    }
    DtTransport t={0};ds3_calls=ds5_calls=0;
    assert(dt_controller_bluetooth_create(NULL,DT_BT_CONTROLLER_DS3)==-1);
    assert(dt_controller_bluetooth_create(&t,99)==-1 && !t.ops && !ds3_calls && !ds5_calls);
    automatic=1;fault=0;
    /* Discover either model, starting with the other. Exactly one window
     * may be alive. A captured first report survives VDA/session handoff. */
    for(target=0;target<2;++target) {
        now=1;ready_at=10000001;
        assert(dt_controller_bluetooth_auto_create(&t,target?DT_BT_CONTROLLER_DS3:DT_BT_CONTROLLER_DUALSENSE)==0);
        assert(t.ops->read_report(t.context,&(DtInputReport){0},20)==-1);
        now=10000000;assert(t.ops->reconnect(t.context)==0);
        assert(live==1);
        now=10000001;assert(t.ops->reconnect(t.context)==0); /* switch only */
        assert(t.ops->reconnect(t.context)==1);
        unsigned before=opens[0]+opens[1];
        now+=UINT64_C(8)*60*60*1000000;
        assert(t.ops->reconnect(t.context)==1);
        DtInputReport first;assert(t.ops->read_report(t.context,&first,4)==1);
        assert(first.format==(target?DT_REPORT_DUALSENSE_BT:DT_REPORT_DS3));
        assert(t.ops->read_report(t.context,&first,4)==1 && opens[0]+opens[1]==before);
        assert(dt_controller_bluetooth_destroy(&t)==0 && !live && !t.ops);
    }
    /* First valid input at the slot boundary wins without a switch. */
    target=0;ready_at=10000001;now=1;
    assert(dt_controller_bluetooth_auto_create(&t,DT_BT_CONTROLLER_DS3)==0);
    unsigned before=opens[1];now=ready_at;
    assert(t.ops->reconnect(t.context)==1 && opens[1]==before);
    assert(dt_controller_bluetooth_quiesce(&t)==0 && !live);
    assert(t.ops->reconnect(t.context)==-1 && opens[1]==before);
    assert(dt_controller_bluetooth_destroy(&t)==0);
    /* Safe unavailability falls back; rollback failure must not. */
    now=1;unavailable=0;
    assert(dt_controller_bluetooth_auto_create(&t,DT_BT_CONTROLLER_DS3)==0);
    assert(live==1 && dt_controller_bluetooth_destroy(&t)==0);
    unavailable=-1;rollback=0;before=opens[1];
    assert(dt_controller_bluetooth_auto_create(&t,DT_BT_CONTROLLER_DS3)==-2 && !t.ops && !live && opens[1]==before);
    rollback=-1;
    /* Failed cleanup latches a fatal condition, with no successor window. */
    target=-1;now=1;assert(dt_controller_bluetooth_auto_create(&t,DT_BT_CONTROLLER_DS3)==0);
    failed_destroy=1;now+=10000000;before=opens[1];
    assert(t.ops->reconnect(t.context)==-1 && !live && opens[1]==before);
    assert(dt_controller_bluetooth_status(&t)==-1);
    assert(dt_controller_bluetooth_destroy(&t)==-1);failed_destroy=0;
    /* USB arrival must not hide a fatal native status behind clean teardown. */
    now=1;assert(dt_controller_bluetooth_auto_create(&t,DT_BT_CONTROLLER_DS3)==0);
    fault=1;before=opens[1];
    assert(dt_controller_bluetooth_quiesce(&t)==-1 && !live && opens[1]==before);
    assert(dt_controller_bluetooth_destroy(&t)==-1);fault=0;
    /* Idle rotations are bounded and do not impose a lifetime limit. */
    now=1;assert(dt_controller_bluetooth_auto_create(&t,DT_BT_CONTROLLER_DS3)==0);
    for(unsigned i=0;i<100;++i) {now+=10000000;assert(t.ops->reconnect(t.context)==0 && live==1);}
    assert(dt_controller_bluetooth_destroy(&t)==0 && !live);
    /* Clock/backend/format failures never turn into model retries. */
    for(unsigned issue=0;issue<3;++issue) {
        now=100;target=0;ready_at=0;
        assert(dt_controller_bluetooth_auto_create(&t,DT_BT_CONTROLLER_DS3)==0);
        before=opens[1];
        if(issue==0)now=99;
        if(issue==1)fault=1;
        if(issue==2)wrong_format=1;
        assert(t.ops->reconnect(t.context)==-1 && dt_controller_bluetooth_status(&t)==-1);
        assert(opens[1]==before && dt_controller_bluetooth_destroy(&t)==-1 && !live);
        fault=wrong_format=0;
    }
    /* Auto uses the last valid model in RAM; no configuration writes. */
    now=1;assert(dt_controller_bluetooth_create(&t,DT_BT_CONTROLLER_AUTO)==0);
    assert(dt_controller_bluetooth_destroy(&t)==0 && !live);
    puts("PASS Bluetooth selection: exclusive model construction and dispatch, including faulty transports");
}
