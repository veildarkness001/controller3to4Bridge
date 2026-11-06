#ifndef DT_PS4_DUALSENSE_BLUETOOTH_H
#define DT_PS4_DUALSENSE_BLUETOOTH_H
#include "dt/controller.h"

/* Experimental 13.52-only classification window. Separate from the DS3
 * profile window: the two windows must never be active together. */
typedef struct { unsigned attempted; int failed_restore; } DtDualsenseBtWindow;
/* Requires a zero-initialized window. Verifies every complete instruction
 * before changing operands; -2 means rollback could not be verified. */
int dt_dualsense_bt_window_begin(DtDualsenseBtWindow *window);
int dt_dualsense_bt_window_end(DtDualsenseBtWindow *window);
/* Exclusive DualSense listener; do not start a DS3 listener concurrently.
 * Does not initiate pairing or persist physical identities. */
int dt_ps4_dualsense_bluetooth_create(DtTransport *out);
int dt_ps4_dualsense_bluetooth_matches(const DtTransport *transport);
int dt_ps4_dualsense_bluetooth_quiesce(DtTransport *transport);
int dt_ps4_dualsense_bluetooth_destroy(DtTransport *transport);
int dt_ps4_dualsense_bluetooth_status(const DtTransport *transport);
/* Counters only; no peer identities, keys or raw packets cross this API. */
typedef struct {
    unsigned valid, basic, full, invalid, own_reports, token_mismatches, dequeues;
} DtDualsenseBtStats;
int dt_ps4_dualsense_bluetooth_stats(const DtTransport *transport, DtDualsenseBtStats *out);
#endif
