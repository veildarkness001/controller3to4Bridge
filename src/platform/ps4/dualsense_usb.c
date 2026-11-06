#include "dualsense_usb.h"
#include "dt/log.h"
#include <ps4/kernel.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

/* Firmware-specific private USB ABI, independently checked on 13.52.
 * Only the current handle's device word is modified, never code or a global
 * quirk table. Kernel pointers and connection generation stay in RAM. */
struct DtDualSenseUsb {
    DtDsUsbWindow window;
    DtUsbHandle *handle;
    DtUsbdApi api;
    int fd;
    uint32_t generation;
    uint64_t device;
    int32_t (*set_config)(DtUsbHandle *,int32_t);
};
static int kread(uint64_t address,void *out,size_t size)
{
    if(address<UINT64_C(0xffff800000000000) || size>64 || address>UINT64_MAX-size)return -1;
    return kernel_copyout((intptr_t)address,out,size);
}
int dt_ps4_dualsense_usb_supported(uint16_t revision)
{ return revision==0x0100 && (kernel_get_fw_version()&0xffff0000u)==0x13520000u; }
static int connection(DtDualSenseUsb *s)
{
    uint32_t current=0;errno=0;
    int rc=ioctl(s->fd,0x4004557eul,&current);int error=errno;
    if(rc==0)return current==s->generation?0:1;
    /* Never turn a generic ioctl failure (e.g. EINVAL/EBADF) into permission
     * to forget a mutation. Confirm a departed control node a second way. */
    if(error!=ENXIO && error!=ENODEV)return -1;
    int config=-1;errno=0;
    rc=ioctl(s->fd,0x40045564ul,&config);error=errno;
    return rc<0 && (error==ENXIO || error==ENODEV)?1:-1;
}
static int resolve_device(DtDualSenseUsb *s,uint64_t *device)
{
    uint64_t desc=(uint64_t)kernel_get_proc_filedesc(getpid()),files=0,file=0,priv=0,back=0,cpd=0,udev=0;
    uint32_t count=0;
    if(kread(desc,&files,8)!=0 || kread(desc+40,&count,4)!=0 || count>65536 || s->fd<0 || (uint32_t)s->fd>=count ||
       kread(files+(uint64_t)s->fd*8,&file,8)!=0 || kread(file+0x38,&priv,8)!=0 ||
       kread(priv,&back,8)!=0 || back!=file || kread(priv+8,&cpd,8)!=0 || kread(cpd+8,&udev,8)!=0)return -1;
    unsigned char descriptor[18]={0},confirm[18]={0};
    if(kread(udev+0x62d,descriptor,sizeof(descriptor))!=0 || descriptor[0]!=18 || descriptor[1]!=1 ||
       descriptor[8]!=0x4c || descriptor[9]!=0x05 || descriptor[10]!=0xe6 || descriptor[11]!=0x0c ||
       descriptor[12]!=0 || descriptor[13]!=1 || kread(udev+0x62d,confirm,sizeof(confirm))!=0 ||
       memcmp(descriptor,confirm,sizeof(descriptor)))return -1;
    *device=udev;return 0;
}
static int identity(void *context)
{
    DtDualSenseUsb *s=context;int connected=connection(s);uint64_t device=0;
    if(connected!=0)return connected;
    if(resolve_device(s,&device)!=0 || device!=s->device)return -1;
    return connection(s);
}
static int snapshot(void *context,DtDsUsbState *out)
{
    DtDualSenseUsb *s=context;
    if(identity(s)!=0 || kread(s->device+0x5fe,out->quirks,8)!=0 ||
       kread(s->device+0x609,out->config,2)!=0 || kread(s->device+0x613,out->counts,2)!=0 || identity(s)!=0)return -1;
    return 0;
}
static int write_quirk(void *context,uint16_t value)
{
    DtDualSenseUsb *s=context;uint16_t current=0xffff;
    if((value!=0 && value!=0x41) || identity(s)!=0 || kread(s->device+0x600,&current,2)!=0 ||
       current!=(value==0?0x41:0) || identity(s)!=0)return -1;
    return kernel_copyin(&value,(intptr_t)(s->device+0x600),sizeof(value));
}
static int set_config(void *context,int value)
{
    DtDualSenseUsb *s=context;
    if(identity(s)!=0 || (value!=0 && value!=1))return -1;
    int rc=s->set_config(s->handle,value);
    dt_log("DualSense USB native configuration=%d result=0x%08x\n",value,(uint32_t)rc);
    return rc;
}
static int direct_config(void *context,int *value)
{
    DtDualSenseUsb *s=context;unsigned char raw=0xa5;
    if(identity(s)!=0)return -1;
    int rc=s->api.control(s->handle,0x80,8,0,0,&raw,1,250);
    if(rc!=1 || identity(s)!=0)return -1;
    *value=raw;return 0;
}
static int guards(void *module)
{
    /* Four complete compare/branch instructions for the skip-config rule. */
    static const unsigned char expected[]={
        0x66,0x41,0x83,0xbd,0xfe,0x05,0,0,0x41,0x74,0x3b,
        0x66,0x41,0x83,0xbd,0x00,0x06,0,0,0x41,0x74,0x30,
        0x66,0x41,0x83,0xbd,0x02,0x06,0,0,0x41,0x74,0x25,
        0x66,0x41,0x83,0xbd,0x04,0x06,0,0,0x41,0x74,0x1a};
    unsigned char actual[sizeof(expected)];
    if(!KERNEL_ADDRESS_IMAGE_BASE || KERNEL_IMAGE_SIZE<0x45ef36+sizeof(expected) ||
       kernel_copyout(KERNEL_ADDRESS_IMAGE_BASE+0x45ef36,actual,sizeof(actual))!=0 || memcmp(actual,expected,sizeof(actual)))return -1;
    static const unsigned char wrapper[]={0x55,0x48,0x89,0xe5,0xe8,0xa7,0x52,0,0,0x89,0xc7,0x5d,0xe9,0x8f,0xfa,0xff,0xff};
    void *address=dlsym(module,"sceUsbdInterruptTransfer");
    return address && !memcmp(address,wrapper,sizeof(wrapper))?0:-1;
}
int dt_ps4_dualsense_usb_begin(DtDualSenseUsb **out,void *module,DtUsbHandle *handle,const DtUsbdApi *api,int interface,uint8_t endpoint)
{
    if(!out)return -1;
    *out=NULL;
    if(!module || !handle || !api || !api->control || interface!=3 || endpoint!=0x84 ||
       !dt_ps4_dualsense_usb_supported(0x0100) || guards(module)!=0)return -1;
    DtDualSenseUsb *s=calloc(1,sizeof(*s));if(!s)return -1;
    s->handle=handle;s->api=*api;s->fd=-1;
    memcpy(&s->fd,(const unsigned char *)handle+0x70,sizeof(s->fd));
    memcpy(&s->generation,(const unsigned char *)handle+0x20,sizeof(s->generation));
    struct stat st;
    void *symbol=dlsym(module,"sceUsbdSetConfiguration");
    _Static_assert(sizeof(symbol)==sizeof(s->set_config),"native function ABI");
    if(!symbol || s->fd<0 || s->fd>4095 || fstat(s->fd,&st)!=0 || !S_ISCHR(st.st_mode) ||
       connection(s)!=0 || resolve_device(s,&s->device)!=0) {free(s);return -1;}
    memcpy(&s->set_config,&symbol,sizeof(symbol));
    int rc=dt_ds_usb_window_begin(&s->window,(DtDsUsbIo){s,identity,snapshot,write_quirk,set_config,direct_config});
    if(rc==0) {
        *out=s;dt_log("DualSense USB per-device configuration window ready; original state retained\n");return 0;
    }
    if(rc==-2) {*out=s;dt_log("DualSense USB configuration rollback uncertain; activation must stop\n");return -2;}
    free(s);return -1;
}
int dt_ps4_dualsense_usb_end(DtDualSenseUsb **owner)
{
    if(!owner || !*owner)return 0;
    DtDualSenseUsb *s=*owner;int rc=dt_ds_usb_window_end(&s->window);
    if(rc==1)dt_log("DualSense USB connection departed; no writes to old or replacement device\n");
    else dt_log("DualSense USB original state restored=%d\n",rc==0);
    free(s);*owner=NULL;return rc<0?-1:0;
}
