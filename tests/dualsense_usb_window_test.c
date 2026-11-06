#include "../src/platform/ps4/dualsense_usb.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    DtDsUsbState state;
    int direct,identity,writes,configs,fail_write,partial_write,fail_config,detach_config,fail_restore;
} Mock;
static void reset(Mock *m)
{*m=(Mock){.state={.quirks={62,65,0,0},.config={0,1}}};}
static int identity(void *p) {return ((Mock *)p)->identity;}
static int snapshot(void *p,DtDsUsbState *out)
{Mock *m=p;if(m->identity)return -1;*out=m->state;return 0;}
static int write_quirk(void *p,uint16_t value)
{
    Mock *m=p;assert(!m->identity);++m->writes;
    if(value==65 && m->fail_restore)return -1;
    if(m->fail_write==m->writes) {if(m->partial_write)m->state.quirks[1]=value;return -1;}
    m->state.quirks[1]=value;return 0;
}
static int config(void *p,int value)
{
    Mock *m=p;assert(!m->identity);++m->configs;
    if(m->configs==m->detach_config) {m->identity=1;return -1;}
    m->state.config[0]=value?0:255;m->state.config[1]=(uint8_t)value;
    m->state.counts[0]=value && !m->state.quirks[1]?4:0;
    m->state.counts[1]=m->state.counts[0];m->direct=value && !m->state.quirks[1];
    return m->configs==m->fail_config?-1:0;
}
static int direct(void *p,int *out) {Mock *m=p;if(m->identity)return -1;*out=m->direct;return 0;}
static DtDsUsbIo io(Mock *m) {return (DtDsUsbIo){m,identity,snapshot,write_quirk,config,direct};}
int main(void)
{
    Mock m;reset(&m);DtDsUsbWindow w={0};DtDsUsbState original=m.state;
    assert(dt_ds_usb_window_end(&w)==0);
    assert(dt_ds_usb_window_begin(&w,io(&m))==0 && m.direct==1 && m.state.counts[0]==4);
    assert(m.state.quirks[0]==62 && m.state.quirks[1]==0);
    assert(dt_ds_usb_window_begin(&w,io(&m))==-1);
    assert(dt_ds_usb_window_end(&w)==0 && !memcmp(&original,&m.state,sizeof(original)) && !m.direct);
    for(int cycle=0;cycle<100;++cycle) {
        assert(dt_ds_usb_window_begin(&w,io(&m))==0);
        assert(dt_ds_usb_window_end(&w)==0);
    }
    for(int step=1;step<=2;++step) {
        reset(&m);w=(DtDsUsbWindow){0};m.fail_config=step;
        assert(dt_ds_usb_window_begin(&w,io(&m))==-1);
        assert(!memcmp(&original,&m.state,sizeof(original)) && !m.direct && !w.dirty);
    }
    for(int partial=0;partial<2;++partial) {
        reset(&m);w=(DtDsUsbWindow){0};m.fail_write=1;m.partial_write=partial;
        assert(dt_ds_usb_window_begin(&w,io(&m))==-1);
        assert(!memcmp(&original,&m.state,sizeof(original)) && !w.dirty);
    }
    reset(&m);w=(DtDsUsbWindow){0};assert(dt_ds_usb_window_begin(&w,io(&m))==0);
    m.identity=1;int writes=m.writes,configs=m.configs;
    assert(dt_ds_usb_window_end(&w)==1 && writes==m.writes && configs==m.configs && !w.dirty);
    /* A replacement has its own original state; never inherit the old window. */
    reset(&m);assert(dt_ds_usb_window_begin(&w,io(&m))==0);assert(dt_ds_usb_window_end(&w)==0);
    reset(&m);w=(DtDsUsbWindow){0};m.detach_config=2;
    assert(dt_ds_usb_window_begin(&w,io(&m))==-1 && !w.dirty);
    reset(&m);w=(DtDsUsbWindow){0};assert(dt_ds_usb_window_begin(&w,io(&m))==0);
    m.identity=-1;writes=m.writes;configs=m.configs;
    assert(dt_ds_usb_window_end(&w)==-1 && w.dirty && m.writes==writes && m.configs==configs);
    m.identity=0;assert(dt_ds_usb_window_end(&w)==0);
    reset(&m);w=(DtDsUsbWindow){0};assert(dt_ds_usb_window_begin(&w,io(&m))==0);
    m.state.quirks[1]=17;assert(dt_ds_usb_window_end(&w)==-1 && m.state.quirks[1]==17);
    m.state.quirks[1]=0;assert(dt_ds_usb_window_end(&w)==0);
    reset(&m);w=(DtDsUsbWindow){0};assert(dt_ds_usb_window_begin(&w,io(&m))==0);
    m.fail_write=m.writes+1;assert(dt_ds_usb_window_end(&w)==-1 && w.dirty);
    m.fail_write=0;assert(dt_ds_usb_window_end(&w)==0);
    reset(&m);w=(DtDsUsbWindow){0};m.state.quirks[0]=0;
    assert(dt_ds_usb_window_begin(&w,io(&m))==-1 && !m.writes && !m.configs);
    reset(&m);w=(DtDsUsbWindow){0};m.fail_config=2;m.fail_restore=1;
    assert(dt_ds_usb_window_begin(&w,io(&m))==-2 && w.dirty);
    m.fail_restore=0;assert(dt_ds_usb_window_end(&w)==0);
    puts("PASS DualSense USB window: configuration, exact restore, partial failures, 100 cycles, unplug/replacement and uncertain/foreign state");
}
