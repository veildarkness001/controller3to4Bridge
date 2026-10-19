#include "dt/vda_events.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    DtVdaEvents e = {0};
    strcpy(e.marker, "test-vda-marker-1");
    const char *added = "#LOGIN MGR# Receive Event : SCE_MBUS_EVENT_DEVICE_ADDED [DeviceId:0x7030d][type:1][subType:2]";
    dt_vda_event_line(&e, added, 1); assert(!e.device_id);
    dt_vda_event_line(&e, "[DualShockToPS4] test-vda-marker-1", 0); assert(!e.marker_seen);
    dt_vda_event_line(&e, "test-vda-marker-1", 0); assert(e.marker_seen);
    dt_vda_event_line(&e, added, 0); assert(!e.device_id);
    dt_vda_event_line(&e, "SCE_MBUS_EVENT_DEVICE_ADDED [DeviceId:0x123][subType:0]", 1);
    assert(!e.device_id);
    dt_vda_event_line(&e, added, 1); assert(e.device_id == 0x7030d);
    dt_vda_event_line(&e, "DEVICE_OWNER_CHANGED [DeviceId:0x999][UserId:0x123]", 0);
    assert(!e.assigned);
    dt_vda_event_line(&e, "DEVICE_OWNER_CHANGED [DeviceId:0x7030d][UserId:0x123]", 0);
    assert(e.assigned && e.user_id == 0x123);
    dt_vda_event_line(&e, "SCE_MBUS_EVENT_DEVICE_ADDED [DeviceId:0x888][subType:2]", 1);
    assert(e.device_id == 0x7030d);
    dt_vda_event_line(&e, "DEVICE_OWNER_CHANGED [DeviceId:0x7030d][UserId:0xffffffff]", 0);
    assert(!e.assigned);
    puts("PASS VDA events: stale/physical/unrelated devices rejected; own assignment and logout recognized");
}
