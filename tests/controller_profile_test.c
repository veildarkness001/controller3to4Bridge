#include "dt/controller_bluetooth.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define PROFILE "work/tests/controller-profile-test"
static void save(const char *text)
{int fd=open(PROFILE,O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0);size_t n=strlen(text);assert(write(fd,text,n)==(ssize_t)n);assert(close(fd)==0);}
int main(void)
{
    DtBluetoothController model=99;
    assert(dt_bluetooth_profile_parse("ds3",3,&model)==0 && model==DT_BT_CONTROLLER_DS3);
    assert(dt_bluetooth_profile_parse(" \tdualsense\r\n",13,&model)==0 && model==DT_BT_CONTROLLER_DUALSENSE);
    const char *bad[]={""," ","dualshock4","DUALSENSE","ds3 dualsense","ds3\ntrailing"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        model=99;assert(dt_bluetooth_profile_parse(bad[i],strlen(bad[i]),&model)==-1 && model==99);
    }
    const char embedded[]={'d','s','3',0};
    assert(dt_bluetooth_profile_parse(embedded,sizeof(embedded),&model)==-1);
    assert(dt_bluetooth_profile_parse(NULL,3,&model)==-1 && dt_bluetooth_profile_parse("ds3",3,NULL)==-1);
    (void)unlink(PROFILE);assert(dt_bluetooth_profile_load(&model)==0 && model==DT_BT_CONTROLLER_DS3);
    save("dualsense\n");assert(dt_bluetooth_profile_load(&model)==0 && model==DT_BT_CONTROLLER_DUALSENSE);
    save("ds3\n");assert(dt_bluetooth_profile_load(&model)==0 && model==DT_BT_CONTROLLER_DS3);
    save("ds4\n");model=99;assert(dt_bluetooth_profile_load(&model)==-1 && model==99);
    save("                                 ds3");assert(dt_bluetooth_profile_load(&model)==-1);
    assert(unlink(PROFILE)==0);
    puts("PASS Bluetooth profile: missing default, explicit models, bounded reads and malformed-input rejection");
}
