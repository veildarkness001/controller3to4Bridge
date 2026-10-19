#include "dt/ps4_bluetooth_probe.h"
#include "dt/ps4_usb.h"
#include "dt/log.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define BACKUP "/data/dualshock_to_ps4/ds3-pairing-backup.bin"
/* Private console-only rollback record. Never fetch it or log its contents.
 * O_EXCL preserves the original host across reruns and interrupted writes. */
typedef struct { char magic[8]; uint8_t identity[6], previous[6]; } PairBackup;
static int save_backup(const PairBackup *record)
{
    int fd=open(BACKUP,O_WRONLY|O_CREAT|O_EXCL,0600);
    if (fd<0) return -1;
    int ok=write(fd,record,sizeof(*record))==sizeof(*record);
    if (fsync(fd)!=0) ok=0;
    if (close(fd)!=0) ok=0;
    if (!ok) return -1; /* Keep partial file: require explicit recovery. */
    fd=open(BACKUP,O_RDONLY); if(fd<0) return -1;
    PairBackup check={0}; uint8_t extra;
    ok=read(fd,&check,sizeof(check))==sizeof(check) && read(fd,&extra,1)==0 && !memcmp(record,&check,sizeof(check));
    if(close(fd)!=0) ok=0;
    memset(&check,0,sizeof(check)); return ok ? 0 : -1;
}
int dt_ps4_bluetooth_pairing_restore(void)
{
    PairBackup backup={0}; int fd=open(BACKUP,O_RDONLY);
    if(fd<0) return 1;
    uint8_t extra, identity[6]={0}, host[6]={0};
    int valid=read(fd,&backup,sizeof(backup))==sizeof(backup) && read(fd,&extra,1)==0 && !memcmp(backup.magic,"DTPAIR1",8);
    if(close(fd)!=0) valid=0;
    DtTransport usb={0}; int status=1; uint64_t now=dt_now_us();
    if(!valid || !now || dt_ps4_usb_open(&usb,now+10000000u)!=0) goto done;
    if(dt_ps4_usb_pairing_read(&usb,identity,host)!=0 || memcmp(identity,backup.identity,6)) goto done;
    int written=dt_ps4_usb_pairing_write(&usb,backup.previous);
    int checked=dt_ps4_usb_pairing_read(&usb,identity,host);
    status=written==0 && checked==0 && !memcmp(identity,backup.identity,6) && !memcmp(host,backup.previous,6) ? 0 : 3;
    dt_log("BT pairing original host restore verified=%d; backup retained\n",status==0);
 done:
    if(usb.ops) usb.ops->close(usb.context);
    memset(&backup,0,sizeof(backup)); memset(identity,0,sizeof(identity)); memset(host,0,sizeof(host));
    return status;
}
int dt_ps4_bluetooth_pairing_probe(void)
{
    uint8_t local[8]={0}, target[6]={0}, check_id[6]={0}, check_host[6]={0};
    PairBackup backup={.magic={'D','T','P','A','I','R','1',0}};
    if(dt_ps4_bluetooth_local_address(local)!=0) return 1;
    for(unsigned i=0;i<6;++i) target[i]=local[5-i]; /* HCI LE -> DS3 F5 BE. */
    DtTransport usb={0}; uint64_t now=dt_now_us();
    int status=1;
    if(!now || dt_ps4_usb_open(&usb,now+10000000u)!=0) goto done;
    if(dt_ps4_usb_pairing_read(&usb,backup.identity,backup.previous)!=0) goto done;
    unsigned identity_nonzero=0, identity_nonff=0;
    for(unsigned i=0;i<6;++i) { identity_nonzero|=backup.identity[i]; identity_nonff|=(uint8_t)~backup.identity[i]; }
    if(!identity_nonzero || !identity_nonff) goto done;
    if(!memcmp(backup.previous,target,6)) {
        dt_log("BT pairing host already matches; no write or backup replacement\n");
        status=0; goto done;
    }
    if(save_backup(&backup)!=0) {
        dt_log("BT pairing backup unavailable/existing/incomplete; no F5 write\n"); goto done;
    }
    dt_log("BT pairing original host backed up privately and verified\n");
    int written=dt_ps4_usb_pairing_write(&usb,target);
    int readback=dt_ps4_usb_pairing_read(&usb,check_id,check_host);
    if(written==0 && readback==0 && !memcmp(check_id,backup.identity,6) && !memcmp(check_host,target,6)) {
        dt_log("BT pairing new host readback verified; radio still untested\n"); status=0;
    } else {
        dt_log("BT pairing failed; attempting original host rollback on same USB handle\n");
        int restored=dt_ps4_usb_pairing_write(&usb,backup.previous);
        int checked=dt_ps4_usb_pairing_read(&usb,check_id,check_host);
        int ok=restored==0 && checked==0 && !memcmp(check_id,backup.identity,6) && !memcmp(check_host,backup.previous,6);
        dt_log("BT pairing rollback verified=%d\n",ok); status=ok ? 1 : 3;
    }
done:
    if(usb.ops) usb.ops->close(usb.context);
    memset(&backup,0,sizeof(backup)); memset(local,0,sizeof(local)); memset(target,0,sizeof(target));
    memset(check_id,0,sizeof(check_id)); memset(check_host,0,sizeof(check_host));
    return status;
}
