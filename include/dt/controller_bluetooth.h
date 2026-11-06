#ifndef DT_CONTROLLER_BLUETOOTH_H
#define DT_CONTROLLER_BLUETOOTH_H
#include "dt/controller.h"

typedef enum { DT_BT_CONTROLLER_DS3=0, DT_BT_CONTROLLER_DUALSENSE=1 } DtBluetoothController;
/* Explicit selection keeps the two native classification windows exclusive. */
int dt_controller_bluetooth_create(DtTransport *out, DtBluetoothController controller);
int dt_controller_bluetooth_status(const DtTransport *transport);
int dt_controller_bluetooth_quiesce(DtTransport *transport);
int dt_controller_bluetooth_destroy(DtTransport *transport);
/* Configuration contains only ds3 or dualsense, never addresses or keys.
 * A missing file preserves the DS3 default; malformed/unreadable files fail. */
int dt_bluetooth_profile_parse(const char *data, size_t size, DtBluetoothController *out);
int dt_bluetooth_profile_load(DtBluetoothController *out);
#endif
