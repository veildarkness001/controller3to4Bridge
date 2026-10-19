#include "dt/ps4_bluetooth_probe.h"
#include "dt/log.h"
#include <ps4/kernel.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* 13.52 RX dispatch: ioctl 0x80186223 -> 0x5b63f6 -> getter
 * 0x5b49e0. Key 3 copies precisely 8 bytes from controller context+0x10.
 * Keep the value in memory only: its address semantics are under study. */
typedef struct { uint32_t key, reserved; uint8_t *buffer; uint64_t size; } HostQuery;
_Static_assert(sizeof(HostQuery)==24 && offsetof(HostQuery,buffer)==8, "host query ABI");
int dt_ps4_bluetooth_local_address(uint8_t out[8])
{
    memset(out,0,8);
    if ((kernel_get_fw_version() & 0xffff0000u)!=0x13520000u) return 1;
    int fd=open("/dev/bt",O_RDWR|O_NONBLOCK);
    if (fd<0) { dt_log("BT host query open failed\n"); return 1; }
    uint8_t first[8]={0}, second[8]={0};
    HostQuery query={.key=3,.buffer=first,.size=sizeof(first)};
    int a=ioctl(fd,UINT32_C(0x80186223),&query), b=-1;
    if (a==0) { query.buffer=second; b=ioctl(fd,UINT32_C(0x80186223),&query); }
    unsigned nonzero=0, nonff=0;
    for (unsigned i=0;i<6;++i) { nonzero|=first[i]; nonff|=(uint8_t)~first[i]; }
    int same=a==0 && b==0 && !memcmp(first,second,sizeof(first));
    int valid=same && nonzero && nonff && !first[6] && !first[7];
    dt_log("BT host query first=0x%08x second=0x%08x stable=%d six_bytes_nontrivial=%d tail_zero=%d; value omitted\n",
           (uint32_t)a,(uint32_t)b,same,!!nonzero && !!nonff,first[6]==0 && first[7]==0);
    int closed=close(fd); dt_log("BT host query close=%d\n",closed);
    if (closed==0 && valid) memcpy(out,first,8);
    memset(first,0,sizeof(first)); memset(second,0,sizeof(second));
    return closed!=0 ? 2 : valid ? 0 : 1;
}
int dt_ps4_bluetooth_host_probe(void)
{
    uint8_t value[8]; int result=dt_ps4_bluetooth_local_address(value);
    memset(value,0,sizeof(value)); return result;
}
