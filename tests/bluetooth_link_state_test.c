#define DT_BT_LINK_STATE_PROBE
#define main dt_probe_main
#define open dt_test_open
#define close dt_test_close
#define flock dt_test_flock
#include "../src/app/bluetooth_probe.c"
#undef main
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
static int scenario, probes, calls, logs, closed;
int dt_test_open(const char *path,int flags,...)
{ (void)flags; assert(strstr(path,".lock")); return 42; }
int dt_test_close(int fd) { assert(fd==42); ++closed; return 0; }
int dt_test_flock(int fd,int flags) { (void)flags; assert(fd==42); return scenario==4 ? -1 : 0; }
uint32_t kernel_get_fw_version(void) { return 0x13520001; }
int dt_log_open_named(const char *name) { assert(!strcmp(name,"bluetooth-probe.log")); ++logs; return 0; }
void dt_log_close(void) {}
void dt_log(const char *fmt,...) { (void)fmt; }
int dt_ps4_bluetooth_known_peer(uint64_t *out) { *out=1;return scenario==3 ? 1 : 0; }
int dt_ps4_bluetooth_link_present(uint64_t peer) { assert(peer==1);++calls;return scenario==2 ? -1 : scenario==1; }
int dt_ps4_bluetooth_link_probe(uint64_t peer) { assert(peer==1);++probes;return 0; }
int main(void)
{
    for(scenario=0;scenario<5;++scenario) {
        probes=calls=logs=closed=0;
        int result=dt_probe_main();
        assert(result==(scenario>=2 ? 1 : 0) && closed==1);
        assert(calls==(scenario<3) && probes==(scenario<2));
        assert(logs==(scenario!=4));
    }
    puts("PASS read-only link probe: present/absent/failed inspection, missing identity and lock exclusion; no registration or VDA APIs linked");
}
