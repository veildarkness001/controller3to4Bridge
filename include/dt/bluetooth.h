#ifndef DT_BLUETOOTH_H
#define DT_BLUETOOTH_H
#include "dt/controller.h"

/* Classic Bluetooth HID, not BLE. These are PSMs, never channel IDs. */
enum { DT_BT_CONTROL_PSM = 0x11, DT_BT_INTERRUPT_PSM = 0x13 };
typedef enum { DT_BT_HID_REPORT = 1, DT_BT_HIDP_SDU = 2 } DtBluetoothFraming;

/* A platform adapter owns discovery, pairing and both channels. It must select
 * the intended Sony DS3 and match control/interrupt to the same physical peer.
 * No addresses or pairing keys cross this interface. All operations are bounded.
 * set_feature accepts report ID first; it completes the entire transaction,
 * including HIDP handshake when applicable, returning report bytes or <0.
 * receive returns 1 complete message, 0 timeout, <0 disconnected/error. Never
 * silently truncate an SDU; fail instead. Timestamp is monotonic microseconds.
 * release is idempotent and also releases partial/failed acquisition resources.
 * The caller retains context until the transport is closed. */
typedef struct {
    int (*acquire)(void *context, unsigned timeout_ms);
    int (*set_feature)(void *context, const uint8_t *report, size_t size,
                       unsigned timeout_ms);
    int (*receive)(void *context, uint8_t *data, size_t capacity, size_t *size,
                   uint64_t *timestamp_us, unsigned timeout_ms);
    void (*release)(void *context);
} DtBluetoothLinkOps;

/* No connection, pairing or platform calls at creation. Independent of USB/VDA.
 * out must be zero-initialized. The experimental PS4 native backend exposes
 * DtTransport directly; this adapter contract remains platform-independent. */
int dt_bluetooth_create(DtTransport *out, const DtBluetoothLinkOps *link,
                        void *context, DtBluetoothFraming framing);

/* Pure protocol helpers; failure never changes out. */
int dt_ds3_bluetooth_unpack(const uint8_t *data, size_t size,
                            DtBluetoothFraming framing, uint64_t timestamp_us,
                            DtInputReport *out);
/* HID feature report (5 bytes) or raw control-channel HIDP SDU (6 bytes).
 * Returns length, or 0 for an invalid framing/output/capacity. */
size_t dt_ds3_bluetooth_enable(uint8_t *out, size_t capacity,
                              DtBluetoothFraming framing);
#endif
