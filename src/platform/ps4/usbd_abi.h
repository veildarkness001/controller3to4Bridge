/* Minimal PS4 sceUsbd/libusb ABI declarations. Sources: OpenOrbis Usbd.h and
 * libusb v1.0.27 libusb.h (LGPL-2.1-or-later); see THIRD_PARTY.md.
 * These are host-memory structures, not packed USB wire descriptors. */
#ifndef DT_USBD_ABI_H
#define DT_USBD_ABI_H
#include <stddef.h>
#include <stdint.h>

typedef struct DtUsbDevice DtUsbDevice;
typedef struct DtUsbHandle DtUsbHandle;
typedef struct {
    uint8_t length, type;
    uint16_t usb;
    uint8_t device_class, subclass, protocol, max_packet;
    uint16_t vendor, product, device;
    uint8_t manufacturer, product_string, serial, configurations;
} DtUsbDescriptor;
typedef struct {
    uint8_t length, type, address, attributes;
    uint16_t max_packet;
    uint8_t interval, refresh, synch;
    const unsigned char *extra;
    int extra_length;
} DtUsbEndpoint;
typedef struct {
    uint8_t length, type, number, alternate, endpoint_count;
    uint8_t interface_class, subclass, protocol, string;
    const DtUsbEndpoint *endpoints;
    const unsigned char *extra;
    int extra_length;
} DtUsbAlternate;
typedef struct { const DtUsbAlternate *alternates; int count; } DtUsbInterface;
typedef struct {
    uint8_t length, type;
    uint16_t total;
    uint8_t interface_count, value, string, attributes, power;
    const DtUsbInterface *interfaces;
    const unsigned char *extra;
    int extra_length;
} DtUsbConfig;

_Static_assert(sizeof(void *) == 8, "PS4 ABI needs 64-bit pointers");
_Static_assert(sizeof(DtUsbDescriptor) == 18, "device descriptor ABI");
_Static_assert(sizeof(DtUsbEndpoint) == 32, "endpoint ABI");
_Static_assert(sizeof(DtUsbAlternate) == 40, "interface ABI");
_Static_assert(sizeof(DtUsbConfig) == 40, "configuration ABI");
_Static_assert(offsetof(DtUsbConfig, interfaces) == 16, "config pointer ABI");

typedef struct {
    int32_t (*init)(void);
    void (*exit)(void);
    int64_t (*list)(DtUsbDevice ***);
    void (*free_list)(DtUsbDevice **, int32_t);
    int32_t (*descriptor)(DtUsbDevice *, DtUsbDescriptor *);
    int32_t (*config)(DtUsbDevice *, DtUsbConfig **);
    void (*free_config)(DtUsbConfig *);
    int32_t (*open)(DtUsbDevice *, DtUsbHandle **);
    void (*close)(DtUsbHandle *);
    int32_t (*claim)(DtUsbHandle *, int32_t);
    int32_t (*release)(DtUsbHandle *, int32_t);
    int32_t (*control)(DtUsbHandle *, uint8_t, uint8_t, uint16_t, uint16_t,
                       unsigned char *, uint16_t, uint32_t);
    int32_t (*interrupt)(DtUsbHandle *, unsigned char, unsigned char *, int32_t,
                         int32_t *, uint32_t);
} DtUsbdApi;
#endif
