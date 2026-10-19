#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../src/platform/ps4/bluetooth_pairing_probe.c"
enum { OK, LOCAL_FAIL, USB_FAIL, READ_FAIL, ALREADY, EXISTS, SHORT_WRITE, SYNC_FAIL, VERIFY_FAIL, SET_FAIL, BAD_READBACK, ROLLBACK_FAIL, ID_CHANGED, RESTORE, WRONG_PAD, ZERO_ID };
static int scenario, opened, closed, writes, reads, backup_exists, filepos, written;
static PairBackup disk;
static const uint8_t original[6]={0x10,0x20,0x30,0x40,0x50,0x60};
static const uint8_t target[6]={6,5,4,3,2,1};
static uint8_t current[6];
static char logs[4096];
void dt_log(const char *f,...) { va_list a;va_start(a,f);size_t n=strlen(logs);vsnprintf(logs+n,sizeof(logs)-n,f,a);va_end(a); }
uint64_t dt_now_us(void) {return 1;}
int dt_ps4_bluetooth_local_address(uint8_t out[8]) { for(unsigned i=0;i<8;++i) out[i]=i<6 ? i+1 : 0; return scenario==LOCAL_FAIL ? -1 : 0; }
static void usb_close(void *p) {assert(p==(void*)1);++closed;}
static const DtTransportOps ops={.close=usb_close};
int dt_ps4_usb_open(DtTransport *t,uint64_t deadline) { assert(deadline==10000001); if(scenario==USB_FAIL) return -1; *t=(DtTransport){DT_TRANSPORT_USB,&ops,(void*)1};++opened;return 0; }
int dt_ps4_usb_pairing_read(DtTransport *t,uint8_t id[6],uint8_t host[6]) {
 assert(t->context==(void*)1); ++reads; if(scenario==READ_FAIL) return -1;
 memset(id,scenario==ZERO_ID ? 0 : 0xaa,6); memcpy(host,current,6);
 if((scenario==ID_CHANGED && reads==2) || scenario==WRONG_PAD) id[0]=0xbb;
 if(scenario==BAD_READBACK && reads==2) host[0]^=1;
 return 0;
}
int dt_ps4_usb_pairing_write(DtTransport *t,const uint8_t host[6]) {
 assert(t->context==(void*)1);++writes;
 assert(backup_exists); /* No mutation before private rollback record exists. */
 if(writes==1 && scenario!=RESTORE) { assert(!memcmp(host,target,6)); assert(written); }
 else assert(!memcmp(host,original,6));
 if((scenario==SET_FAIL && writes==1) || scenario==ROLLBACK_FAIL) return -1;
 memcpy(current,host,6);return 0;
}
int dt_test_open(const char *p,int flags,...) {
 assert(!strcmp(p,BACKUP));filepos=0;
 if(flags==O_RDONLY) return backup_exists ? 7 : -1;
 assert(flags==(O_WRONLY|O_CREAT|O_EXCL)); va_list a;va_start(a,flags);assert(va_arg(a,int)==0600);va_end(a);
 if(backup_exists || scenario==EXISTS) return -1; backup_exists=1;return 7;
}
ssize_t dt_test_write(int fd,const void *p,size_t n) {assert(fd==7 && n==sizeof(disk));memcpy(&disk,p,n);written=1;return scenario==SHORT_WRITE ? (ssize_t)n-1 : (ssize_t)n;}
ssize_t dt_test_read(int fd,void *p,size_t n) {
 assert(fd==7); if(filepos) return 0; assert(n==sizeof(disk));memcpy(p,&disk,n);filepos=1;
 if(scenario==VERIFY_FAIL) ((PairBackup*)p)->magic[0]^=1;
 return n;
}
int dt_test_fsync(int fd) {assert(fd==7);return scenario==SYNC_FAIL ? -1 : 0;}
int dt_test_close(int fd) {assert(fd==7);return 0;}
int main(void) {
 for(scenario=OK;scenario<=ZERO_ID;++scenario) {
  opened=closed=writes=reads=backup_exists=filepos=written=0;logs[0]=0;memcpy(current,original,6);
  if(scenario==ALREADY) memcpy(current,target,6);
  if(scenario==RESTORE || scenario==WRONG_PAD) { backup_exists=1;memcpy(disk.magic,"DTPAIR1",8);memset(disk.identity,0xaa,6);memcpy(disk.previous,original,6);memcpy(current,target,6); }
  int ret=scenario==RESTORE || scenario==WRONG_PAD ? dt_ps4_bluetooth_pairing_restore() : dt_ps4_bluetooth_pairing_probe();
  assert(ret==(scenario==OK || scenario==ALREADY || scenario==RESTORE ? 0 : scenario==ROLLBACK_FAIL ? 3 : 1));
  assert(opened==closed);
  assert(writes==(scenario==OK || scenario==RESTORE ? 1 : scenario==SET_FAIL || scenario==BAD_READBACK || scenario==ROLLBACK_FAIL || scenario==ID_CHANGED ? 2 : 0));
  if(scenario==OK) assert(!memcmp(current,target,6) && !memcmp(disk.previous,original,6));
  if(scenario==SET_FAIL || scenario==BAD_READBACK || scenario==ID_CHANGED || scenario==RESTORE) assert(!memcmp(current,original,6));
  assert(!strstr(logs,"10:20") && !strstr(logs,"102030"));
 }
 puts("PASS pairing transaction: private exclusive backup, verification, rollback, restoration identity guard, cleanup and redaction");
}
