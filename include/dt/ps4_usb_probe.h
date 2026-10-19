#ifndef DT_PS4_USB_PROBE_H
#define DT_PS4_USB_PROBE_H
#include "dt/controller.h"
/* Read-only observations after the input window; never reset or reconfigure. */
void dt_ps4_usb_probe_status(DtTransport *transport);
/* Explicit volatile USB streaming enable, not pairing or device reset. */
int dt_ps4_usb_probe_enable_stream(DtTransport *transport);
#endif
