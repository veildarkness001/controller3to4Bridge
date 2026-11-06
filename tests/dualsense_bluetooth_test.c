#include "dt/dualsense.h"
#include "dualsense_fixtures.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int acquired, released, features, acquire_result=1, feature_result=64, receive_result=1;
static int corrupt_feature, corrupt_input, oversize;
static DtBluetoothFraming framing;
static int acquire(void *c,unsigned timeout)
{assert(c==(void*)1 && timeout==250 && !acquired);acquired=1;return acquire_result;}
static void release(void *c) {assert(c==(void*)1 && acquired);acquired=0;++released;}
static int feature(void *c,uint8_t *r,size_t size,unsigned timeout)
{
    assert(c==(void*)1 && acquired && size==64 && r[0]==0x20 && timeout==250);++features;
    memcpy(r,bt_firmware,64);if(corrupt_feature)r[5]^=1;return feature_result;
}
static int receive(void *c,uint8_t *data,size_t capacity,size_t *size,uint64_t *timestamp,unsigned timeout)
{
    assert(c==(void*)1 && acquired && capacity==79 && timeout==4);
    size_t prefix=framing==DT_BT_HIDP_SDU;
    if(prefix)data[0]=0xa1;
    memcpy(data+prefix,bt_neutral,78);if(corrupt_input)data[prefix+20]^=1;
    *size=oversize ? 80 : 78+prefix;*timestamp=123;return receive_result;
}
int main(void)
{
    DtDualsenseBluetoothLinkOps ops={acquire,feature,receive,release};
    for(framing=DT_BT_HID_REPORT;framing<=DT_BT_HIDP_SDU;++framing) {
        DtTransport t={0};DtInputReport r={0};
        assert(dt_dualsense_bluetooth_create(&t,&ops,(void*)1,framing)==0 && !acquired);
        assert(t.ops->read_report(t.context,&r,4)<0);
        assert(t.ops->reconnect(t.context)==1 && acquired);
        assert(t.ops->read_report(t.context,&r,4)==1 && r.format==DT_REPORT_DUALSENSE_BT);
        assert(t.ops->read_report(t.context,&r,0)<0 && t.ops->read_report(t.context,&r,1001)<0);
        receive_result=0;assert(t.ops->read_report(t.context,&r,4)==0 && acquired);receive_result=1;
        corrupt_input=1;assert(t.ops->read_report(t.context,&r,4)<0 && !acquired);corrupt_input=0;
        assert(t.ops->reconnect(t.context)==1);
        oversize=1;assert(t.ops->read_report(t.context,&r,4)<0 && !acquired);oversize=0;
        for(feature_result=-1;feature_result<64;++feature_result)
            assert(t.ops->reconnect(t.context)<0 && !acquired);
        feature_result=64;corrupt_feature=1;
        assert(t.ops->reconnect(t.context)<0 && !acquired);corrupt_feature=0;
        acquire_result=0;int before=features;
        assert(t.ops->reconnect(t.context)==0 && !acquired && features==before);
        acquire_result=-1;assert(t.ops->reconnect(t.context)<0 && !acquired);acquire_result=1;
        assert(t.ops->reconnect(t.context)==1);
        t.ops->close(t.context);assert(!acquired);
    }
    DtTransport t={0};assert(dt_dualsense_bluetooth_create(&t,NULL,(void*)1,framing)<0);
    ops.get_feature=NULL;assert(dt_dualsense_bluetooth_create(&t,&ops,(void*)1,DT_BT_HID_REPORT)<0);
    assert(released>100);
    puts("PASS DualSense BT adapter: feature activation, CRC, reads, partial acquisition cleanup and recovery (synthetic)");
}
