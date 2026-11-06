#include "dt/controller_bluetooth.h"
#include "dt/ps4_bluetooth.h"
#include "dt/ps4_dualsense_bluetooth.h"
#include <assert.h>
#include <stdio.h>
static const DtTransportOps ds3_ops={0},ds5_ops={0};
static unsigned ds3_calls,ds5_calls;
static int fault;
int dt_ps4_bluetooth_create(DtTransport *t)
{++ds3_calls;*t=(DtTransport){.kind=DT_TRANSPORT_BLUETOOTH,.ops=&ds3_ops};return 0;}
int dt_ps4_dualsense_bluetooth_create(DtTransport *t)
{++ds5_calls;*t=(DtTransport){.kind=DT_TRANSPORT_BLUETOOTH,.ops=&ds5_ops};return 0;}
int dt_ps4_dualsense_bluetooth_matches(const DtTransport *t) {return t && t->ops==&ds5_ops;}
int dt_ps4_bluetooth_status(const DtTransport *t) {assert(t->ops==&ds3_ops);++ds3_calls;return fault?-1:0;}
int dt_ps4_dualsense_bluetooth_status(const DtTransport *t) {assert(t->ops==&ds5_ops);++ds5_calls;return fault?-1:0;}
int dt_ps4_bluetooth_quiesce(DtTransport *t) {assert(t->ops==&ds3_ops);++ds3_calls;return fault?-1:0;}
int dt_ps4_dualsense_bluetooth_quiesce(DtTransport *t) {assert(t->ops==&ds5_ops);++ds5_calls;return fault?-1:0;}
int dt_ps4_bluetooth_destroy(DtTransport *t) {assert(t->ops==&ds3_ops);++ds3_calls;*t=(DtTransport){0};return fault?1:0;}
int dt_ps4_dualsense_bluetooth_destroy(DtTransport *t) {assert(t->ops==&ds5_ops);++ds5_calls;*t=(DtTransport){0};return fault?1:0;}
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
    puts("PASS Bluetooth selection: exclusive model construction and dispatch, including faulty transports");
}
