#ifndef DT_DUALSENSE_H
#define DT_DUALSENSE_H
#include "dt/bluetooth.h"

/* CRC covers the HID transaction seed followed by all bytes except the last
 * four little-endian checksum bytes. Pure validation, no device operations. */
int dt_dualsense_check_crc(uint8_t seed, const uint8_t *data, size_t size);
/* Native output-report request for enhanced BT mode, without feedback flags.
 * Submission alone does not prove that enhanced input was enabled. */
size_t dt_dualsense_bluetooth_enable(uint8_t *out, size_t capacity);
int dt_dualsense_bluetooth_unpack(const uint8_t *data, size_t size,
                                  DtBluetoothFraming framing, uint64_t timestamp_us,
                                  DtInputReport *out);

/* Adapter contract for a positively identified Sony 054c:0ce6 peer. Discovery,
 * pairing, exclusive ownership and HID channels belong to the platform.
 * get_feature uses report-ID-first buffers and completes its HID transaction.
 * The PS4 DS3 native backend does NOT implement this adapter yet. */
typedef struct {
    int (*acquire)(void *context, unsigned timeout_ms);
    int (*get_feature)(void *context, uint8_t *report, size_t size, unsigned timeout_ms);
    int (*receive)(void *context, uint8_t *data, size_t capacity, size_t *size,
                   uint64_t *timestamp_us, unsigned timeout_ms);
    void (*release)(void *context);
} DtDualsenseBluetoothLinkOps;
int dt_dualsense_bluetooth_create(DtTransport *out, const DtDualsenseBluetoothLinkOps *link,
                                  void *context, DtBluetoothFraming framing);
#endif
