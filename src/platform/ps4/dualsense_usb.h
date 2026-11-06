#ifndef DT_DUALSENSE_USB_H
#define DT_DUALSENSE_USB_H
#include "usbd_abi.h"
/* Private platform contract; no kernel pointers or USB types reach decoding. */
typedef struct { uint16_t quirks[4]; uint8_t config[2], counts[2]; } DtDsUsbState;
typedef struct {
    void *context;
    /* 0: same connection, 1: confirmed departed/replaced, -1: uncertain. */
    int (*identity)(void *);
    int (*snapshot)(void *, DtDsUsbState *);
    int (*write_quirk)(void *, uint16_t);
    int (*set_config)(void *, int);
    int (*direct_config)(void *, int *);
} DtDsUsbIo;
typedef struct {
    DtDsUsbIo io;
    DtDsUsbState original;
    int dirty, configured;
} DtDsUsbWindow;
/* begin: 0 ready, -1 safely unavailable, -2 restoration uncertain. */
int dt_ds_usb_window_begin(DtDsUsbWindow *, DtDsUsbIo);
/* end: 0 restored, 1 connection departed without writes, -1 uncertain. */
int dt_ds_usb_window_end(DtDsUsbWindow *);

typedef struct DtDualSenseUsb DtDualSenseUsb;
int dt_ps4_dualsense_usb_supported(uint16_t revision);
int dt_ps4_dualsense_usb_begin(DtDualSenseUsb **, void *module,
                             DtUsbHandle *, const DtUsbdApi *, int interface,
                             uint8_t endpoint);
/* Always releases the private context. Failure must stop further activation. */
int dt_ps4_dualsense_usb_end(DtDualSenseUsb **);
#endif
