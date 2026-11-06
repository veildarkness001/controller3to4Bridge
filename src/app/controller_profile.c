#include "dt/controller_bluetooth.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#ifndef DT_BT_PROFILE_PATH
#define DT_BT_PROFILE_PATH "/data/dualshock_to_ps4/bluetooth-controller"
#endif
static int space(unsigned char c)
{return c==' ' || c=='\t' || c=='\n' || c=='\r';}
int dt_bluetooth_profile_parse(const char *data,size_t size,DtBluetoothController *out)
{
    if(!data || !out || !size || size>32)return -1;
    while(size && space((unsigned char)*data)) {++data;--size;}
    while(size && space((unsigned char)data[size-1]))--size;
    DtBluetoothController selected;
    if(size==3 && !memcmp(data,"ds3",3))selected=DT_BT_CONTROLLER_DS3;
    else if(size==9 && !memcmp(data,"dualsense",9))selected=DT_BT_CONTROLLER_DUALSENSE;
    else return -1;
    *out=selected;return 0;
}
int dt_bluetooth_profile_load(DtBluetoothController *out)
{
    if(!out)return -1;
    int fd=open(DT_BT_PROFILE_PATH,O_RDONLY);
    if(fd<0) {
        if(errno!=ENOENT)return -1;
        *out=DT_BT_CONTROLLER_DS3;return 0;
    }
    char buffer[33];size_t size=0;int result=-1;
    while(size<sizeof(buffer)) {
        ssize_t count=read(fd,buffer+size,sizeof(buffer)-size);
        if(count<0 && errno==EINTR)continue;
        if(count<0)goto done;
        if(!count) {result=0;break;}
        size+=(size_t)count;
    }
done:
    if(close(fd)!=0)result=-1;
    if(result!=0)return -1;
    return dt_bluetooth_profile_parse(buffer,size,out);
}
