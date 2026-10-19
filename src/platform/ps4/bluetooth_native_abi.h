#ifndef DT_BLUETOOTH_NATIVE_ABI_H
#define DT_BLUETOOTH_NATIVE_ABI_H
#include <stdint.h>
#include "dt/controller.h"

/* Derived from the user's firmware 13.52 libSceBluetoothHid.sprx:
 * SHA256 ad959693dbc22735035a02ad91266db05389ca74eeea50bcf87a91e5f8aa37d7.
 * Offsets are library virtual offsets, not live process addresses.
 * RegisterDevice 0xa30 / UnregisterDevice 0xb70, ioctl arguments at 0xa86,
 * 0xa8d / 0xbbc,0xbc3: two uint16_t fields, vendor then product.
 * RegisterCallback 0x536..0x553: 8-byte event subscription write.
 * Watcher 0x15a3..0x1638: 16-byte event records; never log peer token.
 * This is a narrowly scoped experimental ABI, not a portable Sony SDK. */
#define DT_BT_REGISTER_DS3 UINT32_C(0x8004622b)
#define DT_BT_UNREGISTER_DS3 UINT32_C(0x8004622c)
/* RX dispatch0x5b5a83/table0xad5d48 ->0x5b5e3d ->0x5b2cc0; uint64 peer. */
#define DT_BT_DISCONNECT UINT32_C(0x8008620b)
#define DT_BT_HID_EVENT_MASK UINT64_C(0x1ff000000)
typedef struct { uint16_t vendor, product; } DtBtNativeRegistration;
typedef struct {
    uint8_t type, status;
    uint16_t reserved;
    uint32_t vendor_product;
    uint64_t peer_token;
} DtBtNativeEvent;
_Static_assert(sizeof(DtBtNativeRegistration) == 4, "registration ABI");
_Static_assert(sizeof(DtBtNativeEvent) == 16, "event ABI");
/* Optional firmware-specific test only. restored=0 is a stop condition. */
int dt_bt_admitted_ioctl(int fd, unsigned long command,
                         DtBtNativeRegistration *request, int *restored);
typedef struct { uint8_t original; int attempted; } DtBtScanWindow;
int dt_bt_scan_window_begin(int fd, DtBtScanWindow *window);
int dt_bt_scan_window_end(int fd, DtBtScanWindow *window);
typedef struct { unsigned attempted; int failed_restore; } DtBtProfileWindow;
typedef struct { int pid, attempted, failed_restore; uint8_t saved[16]; } DtBtEventWindow;
int dt_bt_event_window_begin(DtBtEventWindow *window);
int dt_bt_event_window_end(DtBtEventWindow *window);
int dt_bt_profile_window_begin(const DtBtNativeRegistration *request, DtBtProfileWindow *window);
int dt_bt_profile_window_end(DtBtProfileWindow *window);
typedef struct { uint64_t peer; void *buffer; uint64_t size; uint8_t report_id, reserved[7]; } DtBtNativeReportRequest;
_Static_assert(sizeof(DtBtNativeReportRequest)==32,"native report ABI");
#define DT_BT_SET_FEATURE UINT32_C(0x80206231)
#define DT_BT_DEQUEUE UINT32_C(0x80206233)
typedef struct {
    uint64_t peer;
    unsigned attempts, completions, valid, invalid, changes, errors;
    int accepted, completed;
    DtControllerState last;
    DtInputReport latest;
} DtBtReportProbe;
void dt_bt_report_probe_event(int fd,uint64_t known_peer,const DtBtNativeEvent *event,DtBtReportProbe *probe);
#endif
