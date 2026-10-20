#include "dt/ps4_bluetooth_probe.h"
#include "dt/log.h"
#include <ps4/kernel.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/* 13.52-only, read-only inspection of this DS3's cache metadata. Layout is
 * derived from RX function0x5b7570:32 entries at context+0x18da8, stride0x408.
 * No names, addresses, keys, foreign-device fields or raw memory are logged.
 * The original-host backup remains on the console; only its DS3 identity is
 * read in memory to restrict the lookup. Never retrieve that backup by FTP. */
static int cache_context(uint64_t *result)
{
    if((kernel_get_fw_version()&0xffff0000u)!=0x13520000u ||
       !KERNEL_ADDRESS_IMAGE_BASE || KERNEL_IMAGE_SIZE<0x2612558) return 1;
    const uint8_t expected[7]={0x48,0x8d,0x05,0xd9,0xda,0x05,0x02};
    uint8_t signature[7];
    if(kernel_copyout(KERNEL_ADDRESS_IMAGE_BASE+0x5b4a70,signature,7)!=0 ||
       memcmp(signature,expected,7)) return 1;
    uint64_t context=0;
    if(kernel_copyout(KERNEL_ADDRESS_IMAGE_BASE+0x2612550,&context,8)!=0 ||
       context<UINT64_C(0xffff800000000000) || context>UINT64_MAX-0x4b7b8 || (context&7)) return 1;
    *result=context; return 0;
}
int dt_ps4_bluetooth_scan_state(uint8_t *out)
{
    uint64_t context=0;
    if(!out || cache_context(&context)!=0 || kernel_copyout(context+0x22b48,out,1)!=0 || *out>3) return 1;
    return 0;
}
int dt_ps4_bluetooth_known_peer(uint64_t *out)
{
    if(!out) return 1;
    *out=0;
    uint8_t record[20]={0}, extra;
    int fd=open("/data/dualshock_to_ps4/ds3-pairing-backup.bin",O_RDONLY);
    if(fd<0) return 1;
    int valid=read(fd,record,sizeof(record))==sizeof(record) && read(fd,&extra,1)==0 &&
              !memcmp(record,"DTPAIR1",8);
    if(close(fd)!=0) valid=0;
    uint64_t identity=0;
    if(valid) for(unsigned i=8;i<14;++i) identity=(identity<<8)|record[i];
    memset(record,0,sizeof(record));
    if(!valid || !identity || identity==UINT64_C(0xffffffffffff)) return 1;
    *out=identity; return 0;
 }
int dt_ps4_bluetooth_link_present(uint64_t peer)
{
    uint64_t context=0;
    if(!peer || (peer>>48) || cache_context(&context)!=0) return -1;
    for(uint64_t offset=0x42a8;offset<0xd960;offset+=0x1588) {
        uint64_t token=0;
        if(kernel_copyout(context+offset+0x30,&token,8)!=0) return -1;
        if((token&UINT64_C(0xffffffffffff))==peer) return 1;
    }
    return 0;
}
int dt_ps4_bluetooth_link_probe(uint64_t peer)
{
    uint64_t context=0;
    if(!peer || (peer>>48) || cache_context(&context)!=0) return 1;
    /* RX0x5ae720: classic slots [0x42a8,0xd960), stride0x1588.
     * RX0x5b38c0/0x5aeef0: eight channel records, stride0x90.
     * Recheck identity after reading; a disappearing slot is inconclusive. */
    for(uint64_t offset=0x42a8;offset<0xd960;offset+=0x1588) {
        uint64_t slot=context+offset, token=0, confirm=0;
        if(kernel_copyout(slot+0x30,&token,8)!=0) return 1;
        if((token&UINT64_C(0xffffffffffff))!=peer) continue;
        uint32_t flags=0,pending=0,deferred=0,blocked=0;
        uint16_t hci_handle=0, psm[8]={0}; uint32_t channel_flags[8]={0};
        if(kernel_copyout(slot+0x18,&flags,4)!=0 ||
           kernel_copyout(slot+0x48,&hci_handle,2)!=0 ||
           kernel_copyout(slot+0x12d0,&pending,4)!=0 ||
           kernel_copyout(slot+0x12e4,&deferred,4)!=0 ||
           kernel_copyout(slot+0x12e8,&blocked,4)!=0) return 1;
        for(unsigned j=0;j<8;++j)
            if(kernel_copyout(slot+0x650+j*0x90+0x3c,&psm[j],2)!=0 ||
               kernel_copyout(slot+0x650+j*0x90+8,&channel_flags[j],4)!=0) return 1;
        if(kernel_copyout(slot+0x30,&confirm,8)!=0 || confirm!=token) return 1;
        dt_log("BT DS3 link flags=0x%x pending=0x%x deferred=0x%x blocked=0x%x\n",flags,pending,deferred,blocked);
        dt_log("BT DS3 link HCI_handle_present=%d connecting_bit=%d exact_identity_match=%d\n",
               hci_handle!=0,(flags&1)!=0,token==peer);
        unsigned channels=0;
        for(unsigned j=0;j<8;++j) if(psm[j]) {
            ++channels;dt_log("BT DS3 channel psm=0x%x flags=0x%x\n",psm[j],channel_flags[j]);
        }
        dt_log("BT DS3 link channels=%u; read-only snapshot\n",channels); return 0;
    }
    dt_log("BT DS3 link absent at snapshot\n"); return 0;
}
int dt_ps4_bluetooth_cache_probe(void)
{
    uint64_t context=0, identity=0;
    if(cache_context(&context)!=0 || dt_ps4_bluetooth_known_peer(&identity)!=0) return 1;
    uint32_t state=0, flags=0; uint8_t scan=0;
    if(kernel_copyout(context,&state,4)!=0 || kernel_copyout(context+8,&flags,4)!=0 ||
       kernel_copyout(context+0x22b48,&scan,1)!=0) return 1;
    dt_log("BT cache host state=0x%x flags=0x%x scan_field=0x%x; read-only\n",state,flags,scan);
    unsigned matches=0;
    for(unsigned i=0;i<32;++i) {
        uint8_t metadata[0x30]={0};
        if(kernel_copyout(context+0x18da8+(uint64_t)i*0x408,metadata,sizeof(metadata))!=0) return 1;
        uint64_t token=0; memcpy(&token,metadata,8);
        if((token&UINT64_C(0xffffffffffff))==identity) {
            uint32_t device_flags; uint16_t vendor,product;
            memcpy(&device_flags,metadata+0x24,4); memcpy(&vendor,metadata+0x28,2); memcpy(&product,metadata+0x2a,2);
            dt_log("BT cache matching DS3 flags=0x%x vendor=0x%04x product=0x%04x type=0x%x\n",
                   device_flags,vendor,product,metadata[0x2f]); ++matches;
        }
        memset(metadata,0,sizeof(metadata));
    }
    dt_log("BT cache summary matching_ds3=%u; no connection inferred\n",matches);
#ifdef DT_BT_CACHE_REJECTIONS
    /* Event9 deduplicates its peer token in this32-entry ring before broadcast
     * (RX0x5b100d..0x5b1082). Compare this DS3 only; never export tokens. */
    unsigned rejected=0;
    for(unsigned i=0;i<32;++i) {
        uint64_t token=0;
        if(kernel_copyout(context+0x4b6b8+(uint64_t)i*8,&token,8)!=0) return 1;
        if((token&UINT64_C(0xffffffffffff))==identity) ++rejected;
    }
    dt_log("BT cache event9 prior_notification_matches_ds3=%u; identifiers omitted\n",rejected);
#endif
    return 0;
}
