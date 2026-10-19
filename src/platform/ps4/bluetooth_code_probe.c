#include "dt/ps4_bluetooth_probe.h"
#include "dt/log.h"
#include <ps4/kernel.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#ifndef DT_VERSION
#define DT_VERSION "0.3.0"
#endif
#ifdef DT_BT_CODE_DETAIL
#define SNIPPET_SIZE 8192
#define SNIPPET_BEFORE 2048
#define SNIPPET_SPACING 4096
#else
#define SNIPPET_SIZE 1024
#define SNIPPET_BEFORE 384
#define SNIPPET_SPACING 512
#endif

/* Fixed ELF64 fields; no writable segment or runtime device data is copied.
 * The SDK already identifies its kernel image by this ELF header. */
typedef struct {
    uint8_t ident[16]; uint16_t type, machine; uint32_t version;
    uint64_t entry, phoff, shoff; uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} CodeElfHeader;
typedef struct {
    uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align;
} CodeElfSegment;
typedef struct { uint64_t offset; uint32_t size, reserved; } CodeRecord;
_Static_assert(sizeof(CodeElfHeader)==64 && sizeof(CodeElfSegment)==56, "ELF64 ABI");
_Static_assert(sizeof(CodeRecord)==16, "code record ABI");

int dt_ps4_bluetooth_code_probe(void)
{
    if ((kernel_get_fw_version() & 0xffff0000u) != 0x13520000u ||
        !KERNEL_ADDRESS_IMAGE_BASE || KERNEL_IMAGE_SIZE < 64 ||
        KERNEL_IMAGE_SIZE > 64u*1024*1024) return 1;
    CodeElfHeader h;
    if (kernel_copyout(KERNEL_ADDRESS_IMAGE_BASE, &h, sizeof(h)) != 0 ||
        memcmp(h.ident,"\177ELF\2\1",6) || h.machine!=62 ||
        h.ehsize!=sizeof(h) || h.phentsize!=sizeof(CodeElfSegment) ||
        !h.phnum || h.phnum>32 || h.phoff>65536 ||
        h.phoff+(uint64_t)h.phnum*sizeof(CodeElfSegment)>KERNEL_IMAGE_SIZE) {
        dt_log("BT code probe invalid ELF metadata; no code read\n"); return 1;
    }
    CodeElfSegment segments[32];
    if (kernel_copyout(KERNEL_ADDRESS_IMAGE_BASE+(intptr_t)h.phoff, segments,
                      h.phnum*sizeof(segments[0])) != 0) return 1;
    uint64_t base = UINT64_MAX;
    for (unsigned i=0;i<h.phnum;++i)
        if (segments[i].type==1 && segments[i].vaddr<base) base=segments[i].vaddr;
    if (base==UINT64_MAX) return 1;
    /* Validate every candidate range before reading any code. */
    uint64_t total=0;
#ifdef DT_BT_CODE_TABLE
#ifndef DT_BT_TABLE_OFFSET
#define DT_BT_TABLE_OFFSET UINT64_C(0xad5cb8)
#endif
#ifndef DT_BT_TABLE_SIZE
#define DT_BT_TABLE_SIZE 144
#endif
    int table_valid=0;
    if (!DT_BT_TABLE_SIZE || DT_BT_TABLE_SIZE>131072) return 1;
#endif
    for (unsigned i=0;i<h.phnum;++i) {
        CodeElfSegment *s=&segments[i];
        if (s->type!=1 || s->flags!=5) continue; /* Only PT_LOAD, R+X, not W. */
        uint64_t offset=s->vaddr-base;
        if (s->filesz>s->memsz || offset>KERNEL_IMAGE_SIZE ||
            s->filesz>KERNEL_IMAGE_SIZE-offset || s->filesz>16u*1024*1024 ||
            total>16u*1024*1024-s->filesz) return 1;
        total+=s->filesz;
#ifdef DT_BT_CODE_TABLE
        if (DT_BT_TABLE_OFFSET>=offset && DT_BT_TABLE_OFFSET-offset<=s->filesz &&
            DT_BT_TABLE_SIZE<=s->filesz-(DT_BT_TABLE_OFFSET-offset)) table_valid=1;
#endif
    }
    if (!total) return 1;
#ifdef DT_BT_CODE_CALLBACKS
    /* RX0x5b1317..0x5b14af dispatches eight {mask,callback} slots.
     * Resolve them in memory only. Export bounded RX excerpts, never pointers
     * or writable contents; validate every target before creating the file. */
    CodeRecord callbacks[8]={0};
    unsigned callback_count=0;
    if(KERNEL_IMAGE_SIZE<0x261c4c0) return 1;
    for(unsigned i=0;i<8;++i) {
        uint64_t slot[2]={0};
        if(kernel_copyout(KERNEL_ADDRESS_IMAGE_BASE+0x261c440+i*16,slot,sizeof(slot))!=0) return 1;
        if(!(slot[0]&(UINT64_C(1)<<5)) || !slot[1]) continue;
        uint64_t image=(uint64_t)KERNEL_ADDRESS_IMAGE_BASE;
        if(slot[1]<image || slot[1]-image>=KERNEL_IMAGE_SIZE) return 1;
        uint64_t offset=slot[1]-image;
        int valid=0;
        for(unsigned j=0;j<h.phnum;++j) {
            const CodeElfSegment *s=&segments[j];
            uint64_t begin=s->vaddr-base;
            if(s->type==1 && s->flags==5 && offset>=begin && offset-begin<s->filesz &&
               s->filesz-(offset-begin)>=4096) valid=1;
        }
        if(!valid) return 1;
        callbacks[callback_count++]=(CodeRecord){.offset=offset,.size=4096};
    }
#endif
#ifdef DT_BT_CODE_TABLE
    if (!table_valid) return 1;
#endif
    int fd=open("/data/dualshock_to_ps4/bt-kernel-code-" DT_VERSION ".bin",O_WRONLY|O_CREAT|O_TRUNC,0600);
    if (fd<0) return 1;
    unsigned found=0;
    int status=0;
#if defined(DT_BT_CODE_CALLBACKS)
    uint8_t callback_code[4096];
    for(unsigned i=0;i<callback_count;++i) {
        CodeRecord *record=&callbacks[i];
        if(kernel_copyout(KERNEL_ADDRESS_IMAGE_BASE+(intptr_t)record->offset,callback_code,sizeof(callback_code))!=0 ||
           write(fd,record,sizeof(*record))!=sizeof(*record) ||
           write(fd,callback_code,sizeof(callback_code))!=sizeof(callback_code)) {status=1;break;}
        ++found;
        dt_log("BT code connection callback=%u offset=0x%llx bytes=%u\n",i,
            (unsigned long long)record->offset,record->size);
    }
    if(close(fd)!=0) status=2;
    dt_log("BT code probe finished candidates=%u status=%d; RX callbacks only\n",found,status);
    return status;
#elif defined(DT_BT_CODE_TABLE)
    uint8_t table[4096];
    CodeRecord record={.offset=DT_BT_TABLE_OFFSET,.size=DT_BT_TABLE_SIZE};
    if (write(fd,&record,sizeof(record))!=sizeof(record)) status=1;
    for (size_t pos=0;pos<record.size && !status;pos+=sizeof(table)) {
        size_t count=record.size-pos<sizeof(table) ? record.size-pos : sizeof(table);
        if (kernel_copyout(KERNEL_ADDRESS_IMAGE_BASE+(intptr_t)(record.offset+pos),table,count)!=0 ||
            write(fd,table,count)!=(ssize_t)count) status=1;
    }
    if (!status) { found=1; dt_log("BT code static range copied bytes=%u\n",record.size); }
    if (close(fd)!=0) status=2;
    dt_log("BT code probe finished candidates=%u status=%d; RX static range only\n",found,status);
    return status;
#else
    uint8_t block[16387], snippet[SNIPPET_SIZE];
    for (unsigned i=0;i<h.phnum && found<16;++i) {
        CodeElfSegment *s=&segments[i];
        if (s->type!=1 || s->flags!=5) continue;
        uint64_t offset=s->vaddr-base, last=UINT64_MAX;
        dt_log("BT code probe RX segment offset=0x%llx bytes=%llu\n",
               (unsigned long long)offset,(unsigned long long)s->filesz);
        for (uint64_t pos=0;pos<s->filesz && found<16;pos+=16384) {
            size_t size=s->filesz-pos<sizeof(block) ? (size_t)(s->filesz-pos) : sizeof(block);
            if (kernel_copyout(KERNEL_ADDRESS_IMAGE_BASE+(intptr_t)(offset+pos),block,size)!=0) { status=1; goto done; }
            for (size_t j=1;j+4<=size && j<=16384 && found<16;++j) {
                /* mov r32, 0x802f3701: error observed in native DS3 registration. */
                if (block[j-1]<0xb8 || block[j-1]>0xbf ||
                    memcmp(block+j,"\x01\x37\x2f\x80",4)) continue;
                uint64_t begin=(pos+j>SNIPPET_BEFORE ? pos+j-SNIPPET_BEFORE : 0)&~UINT64_C(15);
                if (last!=UINT64_MAX && begin<last+SNIPPET_SPACING) continue;
                size_t count=s->filesz-begin<sizeof(snippet) ? (size_t)(s->filesz-begin) : sizeof(snippet);
                if (kernel_copyout(KERNEL_ADDRESS_IMAGE_BASE+(intptr_t)(offset+begin),snippet,count)!=0) { status=1; goto done; }
                CodeRecord record={.offset=offset+begin,.size=(uint32_t)count};
                if (write(fd,&record,sizeof(record))!=sizeof(record) ||
                    write(fd,snippet,count)!=(ssize_t)count) { status=1; goto done; }
                ++found;last=begin;
                dt_log("BT code candidate=%u offset=0x%llx bytes=%u\n",found,
                       (unsigned long long)record.offset,record.size);
            }
        }
    }
done:
    if (close(fd)!=0) status=2;
    dt_log("BT code probe finished candidates=%u status=%d; RX code excerpts only\n",found,status);
    return status;
#endif
}
