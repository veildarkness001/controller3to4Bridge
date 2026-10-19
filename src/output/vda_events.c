#include "dt/vda_events.h"
#include "c4f_vda.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

int dt_vda_menu_event(uint64_t own_device, const char *line)
{
    if (!own_device || !line || strstr(line, "[DualShockToPS4]")) return 0;
    const char *request = strstr(line, "sceMbusDisconnectDevice(");
    if (request) {
        request += strlen("sceMbusDisconnectDevice(");
        while (*request == ' ' || *request == '\t') ++request;
        if (request[0] != '0' || request[1] != 'x' ||
            !isxdigit((unsigned char)request[2])) return 0;
        char *end;
        errno = 0;
        unsigned long long device = strtoull(request + 2, &end, 16);
        if (errno == ERANGE || end - (request + 2) > 16 || !device) return 0;
        while (*end == ' ' || *end == '\t') ++end;
        if (*end != ')') return 0;
        return device == own_device ? 1 : 3;
    }
    if (strstr(line, "SCE_MBUS_EVENT_DEVICE_REMOVED") &&
        c4fKlogDeviceId(line) == own_device) return 2;
    return 0;
}

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
