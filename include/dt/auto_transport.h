#ifndef DT_AUTO_TRANSPORT_H
#define DT_AUTO_TRANSPORT_H
#include "dt/controller.h"

/* Borrows backend lifetimes; the caller closes VDA before destroying them.
 * One active source. USB presence requests a neutral boundary before opening
 * the USB handle, so two sources can never contribute to the same sample. */
typedef struct {
    DtTransport *usb, *bluetooth, *active;
    int (*usb_present)(DtTransport *usb);
    int (*quiesce_bluetooth)(DtTransport *bluetooth);
    uint64_t next_probe;
} DtAutoTransport;
int dt_auto_transport_init(DtTransport *out, DtAutoTransport *state,
                          DtTransport *usb, DtTransport *bluetooth,
                          int (*usb_present)(DtTransport *usb),
                          int (*quiesce_bluetooth)(DtTransport *bluetooth));
#endif
