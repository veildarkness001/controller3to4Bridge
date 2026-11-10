#ifndef DT_CONTROLLER_BLUETOOTH_H
#define DT_CONTROLLER_BLUETOOTH_H
#include "dt/controller.h"

typedef enum { DT_BT_CONTROLLER_DS3=0, DT_BT_CONTROLLER_DUALSENSE=1,
               DT_BT_CONTROLLER_AUTO=2 } DtBluetoothController;
/* Explicit selection keeps the two native classification windows exclusive. */
int dt_controller_bluetooth_create(DtTransport *out, DtBluetoothController controller);
/* Serial discovery before the first valid report. A legacy profile is only
 * a first-model hint; neither native window may overlap the other. */
int dt_controller_bluetooth_auto_create(DtTransport *out, DtBluetoothController preferred);
int dt_controller_bluetooth_status(const DtTransport *transport);
int dt_controller_bluetooth_quiesce(DtTransport *transport);
int dt_controller_bluetooth_destroy(DtTransport *transport);
/* auto (default), ds3 or dualsense; legacy values hint the first model in
 * unified automatic discovery. No addresses/keys; malformed reads fail. */
int dt_bluetooth_profile_parse(const char *data, size_t size, DtBluetoothController *out);
int dt_bluetooth_profile_load(DtBluetoothController *out);
#endif
