#include "dt/vda_events.h"
#include "c4f_vda.h"
#include <stdio.h>
#include <string.h>

void dt_vda_event_line(DtVdaEvents *events, const char *line, int accept_add)
{
    if (strstr(line, "[DualShockToPS4]")) return;
    if (events->marker[0] && strstr(line, events->marker)) events->marker_seen = 1;
    if (accept_add && events->marker_seen && !events->device_id && c4fKlogIsVirtualAdd(line))
        events->device_id = c4fKlogDeviceId(line);
    const char *owner = strstr(line, "DEVICE_OWNER_CHANGED [DeviceId:");
    unsigned long long device;
    unsigned user;
    if (owner && events->device_id &&
        sscanf(owner, "DEVICE_OWNER_CHANGED [DeviceId:0x%llx][UserId:0x%x]", &device, &user) == 2 &&
        device == events->device_id) {
        events->user_id = user;
        events->assigned = user != 0xffffffffu;
    }
}
