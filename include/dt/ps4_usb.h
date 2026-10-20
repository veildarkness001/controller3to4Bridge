#ifndef DT_PS4_USB_H
#define DT_PS4_USB_H
#include "dt/controller.h"

/* Opaque backend: no USB handles/types escape into protocol or virtual pad. */
int dt_ps4_usb_open(DtTransport *out, uint64_t deadline_us);
/* Initializes the backend without waiting for a physical controller. */
int dt_ps4_usb_create(DtTransport *out);
/* Read-only enumeration: no open/claim/activation or pairing writes. */
int dt_ps4_usb_present(DtTransport *transport);
/* Checked final cleanup; clears the transport and reports release/unload failure. */
int dt_ps4_usb_destroy(DtTransport *transport);
/* Pairing experiment only: exact F2/F5 reads and bounded F5 host write. */
int dt_ps4_usb_pairing_read(DtTransport *transport, uint8_t identity[6], uint8_t host[6]);
int dt_ps4_usb_pairing_write(DtTransport *transport, const uint8_t host[6]);
#endif
