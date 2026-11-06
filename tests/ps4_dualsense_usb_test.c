#include "../src/platform/ps4/dualsense_usb.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <dlfcn.h>
static void *test_dlsym(void *,const char *);
static int test_ioctl(int,unsigned long,...);
static int test_fstat(int,struct stat *);
#define dlsym test_dlsym
#define ioctl test_ioctl
#define fstat test_fstat
#include "../src/platform/ps4/dualsense_usb.c"
#undef dlsym
#undef ioctl
#undef fstat
const intptr_t KERNEL_ADDRESS_IMAGE_BASE=(intptr_t)UINT64_C(0xffff800000000000);
const size_t KERNEL_IMAGE_SIZE=0x2000000;
static uint32_t firmware=0x13520001,generation=99;
static unsigned char descmem[64],filesmem[128],filemem[64],privmem[40],cpdmem[32],devmem[0x650],handlemem[0xe8];
static unsigned char guard_bytes[44]={0x66,0x41,0x83,0xbd,0xfe,0x5,0x0,0x0,0x41,0x74,0x3b,0x66,0x41,0x83,0xbd,0x0,0x6,0x0,0x0,0x41,0x74,0x30,0x66,0x41,0x83,0xbd,0x2,0x6,0x0,0x0,0x41,0x74,0x25,0x66,0x41,0x83,0xbd,0x4,0x6,0x0,0x0,0x41,0x74,0x1a};
static const unsigned char wrapper_bytes[]={0x55,0x48,0x89,0xe5,0xe8,0xa7,0x52,0,0,0x89,0xc7,0x5d,0xe9,0x8f,0xfa,0xff,0xff};
static int direct_value,writes,configs,io_error,confirm_error,fail_write,fail_config,guard_bad;
static uint64_t address(unsigned n) {return (uint64_t)KERNEL_ADDRESS_IMAGE_BASE+0x2100000+(uint64_t)n*0x1000;}
static void put64(unsigned char *p,uint64_t v) {memcpy(p,&v,8);}
static void put16(unsigned char *p,uint16_t v) {memcpy(p,&v,2);}
static void reset(void)
{
 memset(descmem,0,sizeof(descmem));memset(filesmem,0,sizeof(filesmem));memset(filemem,0,sizeof(filemem));
 memset(privmem,0,sizeof(privmem));memset(cpdmem,0,sizeof(cpdmem));memset(devmem,0,sizeof(devmem));memset(handlemem,0,sizeof(handlemem));
 firmware=0x13520001;generation=99;direct_value=writes=configs=io_error=confirm_error=fail_write=fail_config=guard_bad=0;
 put64(descmem,address(1));uint32_t count=16;memcpy(descmem+40,&count,4);
 put64(filesmem+7*8,address(2));put64(filemem+0x38,address(3));put64(privmem,address(2));put64(privmem+8,address(4));put64(cpdmem+8,address(5));
 const unsigned char descriptor[18]={18,1,0,2,0,0,0,64,0x4c,5,0xe6,0x0c,0,1,1,2,0,1};
 memcpy(devmem+0x62d,descriptor,18);put16(devmem+0x5fe,62);put16(devmem+0x600,65);devmem[0x609]=0;devmem[0x60a]=1;
 int fd=7;memcpy(handlemem+0x70,&fd,4);memcpy(handlemem+0x20,&generation,4);
}
void dt_log(const char *fmt,...) {(void)fmt;}
uint32_t kernel_get_fw_version(void) {return firmware;}
intptr_t kernel_get_proc_filedesc(pid_t pid) {(void)pid;return (intptr_t)address(0);}
int32_t kernel_copyout(intptr_t source,void *out,size_t n)
{
 uint64_t src=(uint64_t)source;
 if(src==(uint64_t)KERNEL_ADDRESS_IMAGE_BASE+0x45ef36 && n==44) {
  memcpy(out,guard_bytes,n);if(guard_bad)((unsigned char *)out)[0]^=1;return 0;
 }
 const struct {unsigned char *data;size_t size;} blocks[]={{descmem,sizeof(descmem)},{filesmem,sizeof(filesmem)},{filemem,sizeof(filemem)},{privmem,sizeof(privmem)},{cpdmem,sizeof(cpdmem)},{devmem,sizeof(devmem)}};
 for(unsigned i=0;i<6;++i)if(src>=address(i) && src-address(i)<=blocks[i].size && n<=blocks[i].size-(src-address(i))) {memcpy(out,blocks[i].data+(src-address(i)),n);return 0;}
 return -1;
}
int32_t kernel_copyin(const void *in,intptr_t destination,size_t n)
{
 assert((uint64_t)destination==address(5)+0x600 && n==2);++writes;
 memcpy(devmem+0x600,in,2);return writes==fail_write?-1:0;
}
static int test_fstat(int fd,struct stat *st) {assert(fd==7);memset(st,0,sizeof(*st));st->st_mode=S_IFCHR;return 0;}
static int test_ioctl(int fd,unsigned long request,...)
{
 assert(fd==7);va_list ap;va_start(ap,request);void *out=va_arg(ap,void *);va_end(ap);
 if(request==0x4004557eul) {if(io_error) {errno=io_error;return -1;}memcpy(out,&generation,4);return 0;}
 assert(request==0x40045564ul);if(confirm_error) {errno=confirm_error;return -1;}*(int *)out=0;return 0;
}
static int32_t native_set(DtUsbHandle *handle,int32_t value)
{
 assert(handle==(DtUsbHandle *)handlemem);++configs;assert(value==0 || value==1);
 devmem[0x609]=value?0:255;devmem[0x60a]=(unsigned char)value;
 uint16_t q;memcpy(&q,devmem+0x600,2);direct_value=value && q!=65;
 devmem[0x613]=devmem[0x614]=direct_value?4:0;
 return configs==fail_config?-1:0;
}
static int32_t control(DtUsbHandle *h,uint8_t type,uint8_t req,uint16_t value,uint16_t index,unsigned char *data,uint16_t size,uint32_t timeout)
{assert(h==(DtUsbHandle *)handlemem && type==0x80 && req==8 && !value && !index && size==1 && timeout==250);*data=(unsigned char)direct_value;return 1;}
static void *test_dlsym(void *module,const char *name)
{
 assert(module==(void *)1);
 if(!strcmp(name,"sceUsbdInterruptTransfer"))return (void *)wrapper_bytes;
 assert(!strcmp(name,"sceUsbdSetConfiguration"));int32_t (*fn)(DtUsbHandle *,int32_t)=native_set;void *out;memcpy(&out,&fn,sizeof(out));return out;
}
static int begin(DtDualSenseUsb **s)
{const DtUsbdApi api={.control=control};return dt_ps4_dualsense_usb_begin(s,(void *)1,(DtUsbHandle *)handlemem,&api,3,0x84);}
int main(void)
{
 DtDualSenseUsb *s=NULL;reset();assert(begin(&s)==0 && s && direct_value==1 && devmem[0x613]==4);
 assert(dt_ps4_dualsense_usb_end(&s)==0 && !s && !direct_value && devmem[0x600]==65 && devmem[0x613]==0 && devmem[0x5fe]==62);
 reset();firmware=0x14000001;assert(!dt_ps4_dualsense_usb_supported(0x0100));assert(begin(&s)==-1 && !writes);
 reset();assert(!dt_ps4_dualsense_usb_supported(0x0101));guard_bad=1;assert(begin(&s)==-1 && !writes);
 reset();devmem[0x637]=0xf2;assert(begin(&s)==-1 && !writes);
 reset();put64(privmem,0);assert(begin(&s)==-1 && !writes);
 reset();generation=100;assert(begin(&s)==-1 && !writes);
 reset();assert(begin(&s)==0);int before=writes;generation=100;
 assert(dt_ps4_dualsense_usb_end(&s)==0 && writes==before); /* replacement generation */
 reset();assert(begin(&s)==0);before=writes;io_error=confirm_error=ENXIO;
 assert(dt_ps4_dualsense_usb_end(&s)==0 && writes==before); /* departed node */
 reset();assert(begin(&s)==0);before=writes;io_error=EINVAL;
 assert(dt_ps4_dualsense_usb_end(&s)==-1 && writes==before); /* unknown, not detach */
 reset();assert(begin(&s)==0);before=writes;io_error=ENXIO;confirm_error=0;
 assert(dt_ps4_dualsense_usb_end(&s)==-1 && writes==before); /* one signal insufficient */
 reset();fail_write=1;assert(begin(&s)==-1 && !s && devmem[0x600]==65);
 reset();fail_config=2;assert(begin(&s)==-1 && !s && devmem[0x600]==65 && !direct_value);
 reset();assert(begin(&s)==0);put16(devmem+0x600,17);before=writes;
 assert(dt_ps4_dualsense_usb_end(&s)==-1 && writes==before && devmem[0x600]==17);
 puts("PASS native DualSense USB: firmware/code/connection/identity guards, own word only, restore, departure/replacement and uncertain ioctl errors");
}
