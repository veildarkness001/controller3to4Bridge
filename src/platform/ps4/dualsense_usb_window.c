#include "dualsense_usb.h"
#include <string.h>
_Static_assert(sizeof(DtDsUsbState)==12,"snapshot has no padding");
static int departed(DtDsUsbWindow *w)
{
    if(w->io.identity(w->io.context)!=1)return 0;
    w->dirty=w->configured=0;
    return 1;
}
int dt_ds_usb_window_end(DtDsUsbWindow *w)
{
    if(!w || (!w->dirty && !w->configured))return 0;
    int identity=w->io.identity(w->io.context);
    if(identity==1) {w->dirty=w->configured=0;return 1;}
    if(identity!=0)return -1;
    int failed=0,value=-1;DtDsUsbState state={0};
    if(w->configured) {
        if(w->io.set_config(w->io.context,0)!=0 ||
           w->io.direct_config(w->io.context,&value)!=0 || value!=0)failed=1;
        if(departed(w))return 1;
    }
    if(w->io.identity(w->io.context)!=0 || w->io.snapshot(w->io.context,&state)!=0)return departed(w)?1:-1;
    if(state.quirks[1]!=0 && state.quirks[1]!=0x41)return -1;
    if(state.quirks[1]==0 && w->io.write_quirk(w->io.context,0x41)!=0)failed=1;
    if(departed(w))return 1;
    if(w->io.snapshot(w->io.context,&state)!=0 || state.quirks[1]!=0x41)return -1;
    /* Restore the cached configuration only after its skip rule is restored. */
    if(w->configured && w->io.set_config(w->io.context,1)!=0)failed=1;
    if(departed(w))return 1;
    if(w->io.identity(w->io.context)!=0 || w->io.snapshot(w->io.context,&state)!=0 ||
       memcmp(&state,&w->original,sizeof(state)) ||
       w->io.direct_config(w->io.context,&value)!=0 || value!=0)return -1;
    w->dirty=w->configured=0;
    return failed?-1:0;
}
int dt_ds_usb_window_begin(DtDsUsbWindow *w,DtDsUsbIo io)
{
    if(!w || w->dirty || w->configured || !io.identity || !io.snapshot ||
       !io.write_quirk || !io.set_config || !io.direct_config)return -1;
    DtDsUsbState state={0};int value=-1;
    const DtDsUsbState expected={.quirks={0x3e,0x41,0,0},.config={0,1},.counts={0,0}};
    if(io.identity(io.context)!=0 || io.snapshot(io.context,&state)!=0 ||
       memcmp(&state,&expected,sizeof(state)) || io.direct_config(io.context,&value)!=0 || value!=0)return -1;
    w->io=io;w->original=state;w->dirty=1;
    if(io.write_quirk(io.context,0)!=0)goto fail;
    if(io.identity(io.context)!=0 || io.snapshot(io.context,&state)!=0 ||
       state.quirks[0]!=0x3e || state.quirks[1]!=0 || state.quirks[2] || state.quirks[3])goto fail;
    w->configured=1; /* Even a failed native request may have changed state. */
    if(io.set_config(io.context,0)!=0 || io.direct_config(io.context,&value)!=0 || value!=0)goto fail;
    if(io.identity(io.context)!=0 || io.set_config(io.context,1)!=0 ||
       io.direct_config(io.context,&value)!=0 || value!=1)goto fail;
    if(io.identity(io.context)!=0 || io.snapshot(io.context,&state)!=0 ||
       state.config[0]!=0 || state.config[1]!=1 || state.counts[0]!=4 || state.counts[1]<2 || state.counts[1]>16 ||
       state.quirks[0]!=0x3e || state.quirks[1]!=0 || state.quirks[2] || state.quirks[3])goto fail;
    return 0;
fail:
    return dt_ds_usb_window_end(w)<0?-2:-1;
}
