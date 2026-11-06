#include "dt/controller_bluetooth.h"
#include "dt/ps4_bluetooth.h"
#include "dt/ps4_dualsense_bluetooth.h"

int dt_controller_bluetooth_create(DtTransport *out,DtBluetoothController controller)
{
    if(!out || out->ops)return -1;
    if(controller==DT_BT_CONTROLLER_DS3)return dt_ps4_bluetooth_create(out);
    if(controller==DT_BT_CONTROLLER_DUALSENSE)return dt_ps4_dualsense_bluetooth_create(out);
    return -1;
}
int dt_controller_bluetooth_status(const DtTransport *t)
{
    return dt_ps4_dualsense_bluetooth_matches(t)?dt_ps4_dualsense_bluetooth_status(t):dt_ps4_bluetooth_status(t);
}
int dt_controller_bluetooth_quiesce(DtTransport *t)
{
    return dt_ps4_dualsense_bluetooth_matches(t)?dt_ps4_dualsense_bluetooth_quiesce(t):dt_ps4_bluetooth_quiesce(t);
}
int dt_controller_bluetooth_destroy(DtTransport *t)
{
    return dt_ps4_dualsense_bluetooth_matches(t)?dt_ps4_dualsense_bluetooth_destroy(t):dt_ps4_bluetooth_destroy(t);
}
