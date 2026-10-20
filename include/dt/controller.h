#ifndef DT_CONTROLLER_H
#define DT_CONTROLLER_H

#include <stddef.h>
#include <stdint.h>

/* DS3 semantic button bits: Select, L3, R3, Start, Up, Right, Down, Left,
 * L2, R2, L1, R1, Triangle, Circle, Cross, Square, PS. Not PS4 button bits. */
typedef struct {
    uint32_t buttons;
    uint8_t lx, ly, rx, ry, l2, r2;
    uint8_t pressure[12];
    uint8_t battery_raw;
    uint16_t motion_raw[4]; /* Retain sensor samples; no DS4 motion conversion yet. */
    uint64_t timestamp_us;
} DtControllerState;

typedef enum { DT_TRANSPORT_USB = 1, DT_TRANSPORT_BLUETOOTH = 2,
               DT_TRANSPORT_AUTO = 3 } DtTransportKind;
typedef struct {
    uint8_t data[128]; /* HID report ID first; backend removes transport framing. */
    size_t size;
    uint64_t timestamp_us;
} DtInputReport;

typedef struct { uint8_t large_motor, small_motor, player; } DtFeedback;
typedef struct {
    /* 1 report, 0 no report/timeout, negative error. Must obey timeout_ms. */
    int (*read_report)(void *context, DtInputReport *report, unsigned timeout_ms);
    /* Optional. A missing callback means feedback is not implemented. */
    int (*set_feedback)(void *context, const DtFeedback *feedback);
    void (*close)(void *context);
    /* Optional recovery, called only while disconnected. Releases any old
     * physical handle, scans once and returns 1 ready, 0 absent, <0 retryable.
     * Does not recreate the virtual controller or reload the USB library. */
    int (*reconnect)(void *context);
} DtTransportOps;

typedef struct {
    DtTransportKind kind;
    const DtTransportOps *ops;
    void *context;
} DtTransport;

/* Rejects truncated, wrong-ID and non-input packets without altering out. */
int dt_ds3_decode(const DtInputReport *report, DtControllerState *out);

#endif
