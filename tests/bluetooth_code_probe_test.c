#include "../src/platform/ps4/bluetooth_code_probe.c"
#include <assert.h>
#include <stdio.h>

#ifdef DT_BT_CODE_CALLBACKS
const size_t KERNEL_IMAGE_SIZE=0x3000000;
#else
const size_t KERNEL_IMAGE_SIZE=65536;
#endif
const intptr_t KERNEL_ADDRESS_IMAGE_BASE=0x100000;
static uint8_t image[65536], output[32768];
static size_t used;
static int scenario, opens, closes, code_reads;
void dt_log(const char *format, ...) { (void)format; }
uint32_t kernel_get_fw_version(void) { return scenario==1 ? 0x14000001 : 0x13520001; }
int32_t kernel_copyout(intptr_t address, void *out, size_t size)
{
    assert(address>=KERNEL_ADDRESS_IMAGE_BASE);
    size_t offset=(size_t)(address-KERNEL_ADDRESS_IMAGE_BASE);
#ifdef DT_BT_CODE_CALLBACKS
    if(offset>=0x261c440 && offset<0x261c4c0) {
        assert(size==16 && !opens);
        if(scenario==13)return -1;
        uint64_t slot[2]={0};
        if(offset==0x261c440) {
            slot[0]=UINT64_C(1)<<5;
            slot[1]=KERNEL_ADDRESS_IMAGE_BASE+(scenario==11 ? KERNEL_IMAGE_SIZE : scenario==12 ? 0x3800 : 0x1800);
        }
        memcpy(out,slot,16);return 0;
    }
#endif
    assert(offset<=sizeof(image) && size<=sizeof(image)-offset);
    if (offset>=0x1000) {
        assert(offset<0x3000 && size<=0x3000-offset); /* No writable-segment reads. */
        ++code_reads;
    } else assert(offset==0 || offset==64); /* Only ELF metadata. */
    if (scenario==8 && offset>=0x1000) return -1;
    memcpy(out,image+offset,size); return 0;
}
int dt_test_open(const char *path, int flags, ...)
{
    assert(!strcmp(path,"/data/dualshock_to_ps4/bt-kernel-code-0.3.0.bin"));
    assert(flags==(O_WRONLY|O_CREAT|O_TRUNC)); ++opens; return 9;
}
ssize_t dt_test_write(int fd, const void *data, size_t size)
{
    assert(fd==9 && used+size<=sizeof(output));
    if (scenario==9) return -1;
    memcpy(output+used,data,size); used+=size; return (ssize_t)size;
}
int dt_test_close(int fd) { assert(fd==9); ++closes; return scenario==10 ? -1 : 0; }
int main(void)
{
    for (scenario=0;scenario<=
#ifdef DT_BT_CODE_CALLBACKS
         13
#else
         10
#endif
         ;++scenario) {
        memset(image,0,sizeof(image));used=0;opens=closes=code_reads=0;
        CodeElfHeader h={.machine=62,.ehsize=64,.phoff=64,.phnum=3,.phentsize=56};
        memcpy(h.ident,"\177ELF\2\1",6);
        CodeElfSegment segs[3]={{.type=1,.flags=4,.vaddr=0,.filesz=0x1000,.memsz=0x1000},
            {.type=1,.flags=5,.vaddr=0x1000,.filesz=0x2000,.memsz=0x2000},
            {.type=1,.flags=6,.vaddr=0x3000,.filesz=0x3000,.memsz=0x3000}};
        if (scenario==2) h.phnum=33;
        if (scenario==3) segs[1].flags=7; /* W+X must not be copied. */
        if (scenario==4) segs[1].filesz=65536;
        if (scenario==5) h.ident[0]=0;
        if (scenario==6) h.phoff=65537;
        if (scenario==7) segs[1].vaddr=UINT64_MAX-32;
        memcpy(image,&h,sizeof(h));memcpy(image+64,segs,sizeof(segs));
        memcpy(image+0x1800,"\xb8\x01\x37\x2f\x80",5);
        memcpy(image+0x3800,"\xb8\x01\x37\x2f\x80" "DO-NOT-COPY-WRITABLE",25);
        int ret=dt_ps4_bluetooth_code_probe();
        assert(ret==(scenario==0 ? 0 : scenario==10 ? 2 : 1));
        assert(opens==(scenario==0 || (scenario>=8 && scenario<=10)));assert(closes==opens);
        if (scenario==0) {
#ifdef DT_BT_CODE_CALLBACKS
            assert(used==4112 && code_reads==1);
#elif defined(DT_BT_CODE_TABLE)
            assert(used==DT_BT_TABLE_SIZE+16 && code_reads==(DT_BT_TABLE_SIZE+4095)/4096);
#else
            assert(used==SNIPPET_SIZE+16 && code_reads==2);
#endif
            CodeRecord r;memcpy(&r,output,16);
#ifdef DT_BT_CODE_CALLBACKS
            assert(r.offset==0x1800 && r.size==4096);
#elif defined(DT_BT_CODE_TABLE)
            assert(r.offset==DT_BT_TABLE_OFFSET && r.size==DT_BT_TABLE_SIZE);
#else
            assert(r.offset>=0x1000 && r.offset+r.size<=0x3000 && r.size==SNIPPET_SIZE);
#endif
            for (size_t i=0;i+8<=used;++i) assert(memcmp(output+i,"DO-NOT-C",8));
        }
    }
    puts("PASS Bluetooth code probe: ELF bounds, firmware gate, RX-only ranges, writable-data exclusion and failure cleanup");
}
