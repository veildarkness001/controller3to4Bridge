#include "dt/ps4_bluetooth_probe.h"
#include <ps4/kernel.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int scenario, auth_writes, cap_writes, called;
static uint8_t original_caps[16] = {1,2,3,4};
void dt_log(const char *format, ...) { (void)format; }
uint64_t kernel_get_ucred_authid(pid_t pid) { (void)pid; return scenario==1 ? 0 : 123; }
int kernel_get_ucred_caps(pid_t pid, uint8_t *caps)
{ (void)pid; memcpy(caps,original_caps,16); return scenario==2 ? -1 : 0; }
int kernel_set_ucred_authid(pid_t pid, uint64_t value)
{
    (void)pid; ++auth_writes;
    assert(value == (auth_writes==1 ? UINT64_C(0x3800000000010003) : 123));
    return (scenario==3 && auth_writes==1) || (scenario==5 && auth_writes==2) ? -1 : 0;
}
int kernel_set_ucred_caps(pid_t pid, const uint8_t *caps)
{
    (void)pid; ++cap_writes;
    int restoring = scenario==3 || cap_writes==2;
    if (restoring) assert(!memcmp(caps,original_caps,16));
    else for (int i=0;i<16;++i) assert(caps[i]==0xff);
    return (scenario==4 && !restoring) || (scenario==6 && restoring) ? -1 : 0;
}
int dt_ps4_bluetooth_native_probe(void) { ++called; return scenario==7 ? 2 : 0; }
int main(void)
{
    for (scenario=0;scenario<=7;++scenario) {
        auth_writes=cap_writes=called=0;
        int result=dt_ps4_bluetooth_privileged_probe();
        assert(result == (scenario==0 ? 0 : scenario==5 || scenario==6 ? 3 : scenario==7 ? 2 : 1));
        assert(called==(scenario==0 || scenario>=5));
        assert(auth_writes==(scenario==1 || scenario==2 ? 0 : 2));
        assert(cap_writes==(scenario==1 || scenario==2 ? 0 : scenario==3 ? 1 : 2));
    }
    puts("PASS Bluetooth privileged comparison: snapshot guards, partial setters, all restorations and cleanup failure propagation");
}
