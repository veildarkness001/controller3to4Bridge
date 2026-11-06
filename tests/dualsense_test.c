#include "dt/dualsense.h"
#include "dt/ps4_mapping.h"
#include "dualsense_fixtures.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static DtInputReport neutral(void)
{
    DtInputReport r = {.size=64, .format=DT_REPORT_DUALSENSE_USB, .timestamp_us=100};
    r.data[0]=1;
    memset(r.data+1,128,4); r.data[8]=8;
    r.data[33]=r.data[37]=0x80;
    return r;
}
int main(void)
{
    DtInputReport r=neutral(); DtControllerState s;
    assert(dt_controller_decode(&r,&s)==0 && !s.buttons && s.native_touch && !s.touch_count);
    assert(s.lx==128 && s.ry==128 && s.timestamp_us==100);
    const unsigned offsets[]={8,8,8,8,9,9,9,9,9,9,9,9,10};
    const unsigned masks[]={16,32,64,128,1,2,4,8,16,32,64,128,1};
    const unsigned semantic[]={15,14,13,12,10,11,8,9,0,3,1,2,16};
    for(unsigned i=0;i<13;++i) {
        r=neutral(); r.data[offsets[i]]|=(uint8_t)masks[i];
        assert(dt_controller_decode(&r,&s)==0 && s.buttons==(1u<<semantic[i]));
    }
    const unsigned hats[]={0x10,0x30,0x20,0x60,0x40,0xc0,0x80,0x90,0,0,0,0,0,0,0,0};
    for(unsigned i=0;i<16;++i) {
        r=neutral(); r.data[8]=(uint8_t)i;
        assert(dt_controller_decode(&r,&s)==0 && s.buttons==hats[i]);
    }
    r=neutral();r.data[1]=0;r.data[2]=255;r.data[3]=7;r.data[4]=248;
    r.data[5]=37;r.data[6]=201;r.data[10]=6;r.data[53]=0x25;
    r.data[16]=0xff;r.data[17]=0xff;r.data[22]=0;r.data[23]=0x80;
    /* One corner contact and one center-ish contact, distinct IDs. */
    r.data[33]=7;r.data[34]=0x7f;r.data[35]=0x77;r.data[36]=0x43; /* 1919,1079 */
    r.data[37]=22;r.data[38]=0xc0;r.data[39]=0xc3;r.data[40]=0x21; /* 960,540 */
    assert(dt_controller_decode(&r,&s)==0);
    assert(s.lx==0 && s.ly==255 && s.rx==7 && s.ry==248 && s.l2==37 && s.r2==201);
    assert(s.gyro_raw[0]==-1 && s.accel_raw[0]==-32768 && s.battery_raw==0x25);
    assert(s.touch_count==2 && s.touch[0].x==1919 && s.touch[0].y==941 && s.touch[1].id==22);
    assert(s.touch_click && s.mute_button);
    DtPs4Mapping m={.held=1,.used=0,.share_until_us=999999}; ScePadData pad;
    s.buttons=1|4; /* Create and R3 are independent of touch. */
    dt_ps4_map(&m,&s,1000,&pad);
    assert(pad.buttons==(1|4|0x100000) && pad.rx==7 && pad.ry==248);
    assert(pad.touchData.fingers==2 && pad.touchData.touch[0].finger==7 && pad.touchData.touch[1].x==960);
    r=neutral();assert(dt_controller_decode(&r,&s)==0);
    dt_ps4_map(&m,&s,2000,&pad);
    assert(!pad.buttons && !pad.touchData.fingers);
    DtControllerState saved=s;
    for(size_t n=0;n<=129;++n) {
        r.size=n;if(n==64)continue;
        assert(dt_controller_decode(&r,&s)<0 && !memcmp(&saved,&s,sizeof(s)));
    }
    r=neutral();r.data[0]=2;assert(dt_controller_decode(&r,&s)<0);
    r=neutral();r.format=99;assert(dt_controller_decode(&r,&s)<0);
    r=neutral();assert(dt_ds3_decode(&r,&s)<0); /* Both use report ID 1. */
    r=neutral();r.data[33]=1;r.data[35]=0x0f;assert(dt_controller_decode(&r,&s)<0);
    r=neutral();r.data[33]=1;r.data[36]=0xff;assert(dt_controller_decode(&r,&s)<0);
    assert(dt_controller_decode(NULL,&s)<0 && dt_controller_decode(&r,NULL)<0);
    DtInputReport bt={.size=78,.format=DT_REPORT_DUALSENSE_BT};
    memcpy(bt.data,bt_neutral,78);assert(dt_controller_decode(&bt,&s)==0 && s.lx==128);
    for(unsigned i=0;i<78;++i) {
        bt.data[i]^=1;assert(dt_controller_decode(&bt,&s)<0);bt.data[i]^=1;
    }
    bt.format=DT_REPORT_DUALSENSE_USB;assert(dt_controller_decode(&bt,&s)<0);
    bt.format=DT_REPORT_DS3;assert(dt_controller_decode(&bt,&s)<0);
    uint8_t hidp[79]={0xa1};memcpy(hidp+1,bt_neutral,78);
    DtInputReport out={0};
    assert(dt_dualsense_bluetooth_unpack(hidp,79,DT_BT_HIDP_SDU,123,&out)==0);
    assert(out.format==DT_REPORT_DUALSENSE_BT && out.timestamp_us==123);
    DtInputReport before=out;
    for(size_t n=0;n<79;++n)
        assert(dt_dualsense_bluetooth_unpack(hidp,n,DT_BT_HIDP_SDU,123,&out)<0 && !memcmp(&out,&before,sizeof(out)));
    hidp[0]=0x52;assert(dt_dualsense_bluetooth_unpack(hidp,79,DT_BT_HIDP_SDU,123,&out)<0);
    assert(dt_dualsense_bluetooth_unpack(bt_neutral,78,DT_BT_HID_REPORT,0,&out)<0);
    assert(dt_dualsense_bluetooth_unpack(bt_neutral,78,99,123,&out)<0);
    assert(dt_dualsense_check_crc(0xa3,bt_firmware,64)==0);
    assert(dt_dualsense_check_crc(0xa1,bt_firmware,64)<0);
    DtInputReport simple={.size=10,.format=DT_REPORT_DUALSENSE_BT,.timestamp_us=55};
    const uint8_t simple_bytes[]={1,128,0,255,64,0x28,0x90,0xfd,32,224};
    memcpy(simple.data,simple_bytes,sizeof(simple_bytes));
    assert(dt_controller_decode(&simple,&s)==0);
    assert(s.lx==128 && s.ly==0 && s.rx==255 && s.ry==64 && s.l2==32 && s.r2==224);
    assert(s.buttons==((1u<<14)|1|4|(1u<<16)) && s.native_touch && !s.touch_count && !s.touch_click);
    simple.data[7]=2;assert(dt_controller_decode(&simple,&s)==0 && s.touch_click && !s.mute_button);
    simple.format=DT_REPORT_DUALSENSE_USB;assert(dt_controller_decode(&simple,&s)<0);
    uint8_t enable[79];memset(enable,0xcc,sizeof(enable));
    assert(dt_dualsense_bluetooth_enable(enable,78)==78 && enable[0]==0x31 && enable[2]==0x10 && enable[78]==0xcc);
    for(unsigned i=3;i<74;++i)assert(!enable[i]);
    assert(dt_dualsense_check_crc(0xa2,enable,78)==0 && dt_dualsense_check_crc(0xa1,enable,78)<0);
    assert(!dt_dualsense_bluetooth_enable(enable,77) && !dt_dualsense_bluetooth_enable(NULL,78));
    puts("PASS DualSense: controls, native touch, raw sensors, identity, strict lengths, CRC and HIDP framing");
}
