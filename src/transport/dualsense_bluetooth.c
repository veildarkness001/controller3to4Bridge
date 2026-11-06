#include "dt/dualsense.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    DtDualsenseBluetoothLinkOps link;
    void *context;
    DtBluetoothFraming framing;
    int acquired, ready;
} DualsenseBluetooth;

static void release_link(DualsenseBluetooth *b)
{
    if (b->acquired) b->link.release(b->context);
    b->acquired = b->ready = 0;
}
static int reconnect(void *context)
{
    DualsenseBluetooth *b = context;
    release_link(b);
    b->acquired = 1;
    int result = b->link.acquire(b->context, 250);
    if (result != 1) { release_link(b); return result == 0 ? 0 : -1; }
    /* Reading firmware feature 20 enables full BT reports; unlike DS3 there
     * is no F4 feature write. Feature bytes may contain private information. */
    uint8_t feature[64] = {0x20};
    result = b->link.get_feature(b->context, feature, sizeof(feature), 250);
    int valid = result == sizeof(feature) && feature[0] == 0x20 &&
                dt_dualsense_check_crc(0xa3, feature, sizeof(feature)) == 0;
    memset(feature, 0, sizeof(feature));
    if (!valid) { release_link(b); return -1; }
    b->ready = 1;
    return 1;
}
static int read_report(void *context, DtInputReport *out, unsigned timeout_ms)
{
    DualsenseBluetooth *b = context;
    if (!out || !timeout_ms || timeout_ms > 1000 || !b->ready) return -1;
    uint8_t data[79];
    size_t size = 0;
    uint64_t timestamp = 0;
    int result = b->link.receive(b->context, data, sizeof(data), &size, &timestamp, timeout_ms);
    if (!result) return 0;
    if (result != 1 || size > sizeof(data) ||
        dt_dualsense_bluetooth_unpack(data, size, b->framing, timestamp, out) != 0) {
        release_link(b);
        return -1;
    }
    return 1;
}
static void close_transport(void *context)
{
    DualsenseBluetooth *b = context;
    release_link(b);
    free(b);
}
int dt_dualsense_bluetooth_create(DtTransport *out, const DtDualsenseBluetoothLinkOps *link,
                                  void *context, DtBluetoothFraming framing)
{
    if (!out || out->ops || !link || !link->acquire || !link->get_feature ||
        !link->receive || !link->release ||
        (framing != DT_BT_HID_REPORT && framing != DT_BT_HIDP_SDU)) return -1;
    DualsenseBluetooth *b = calloc(1, sizeof(*b));
    if (!b) return -1;
    b->link = *link; b->context = context; b->framing = framing;
    static const DtTransportOps ops = {
        .read_report = read_report, .reconnect = reconnect, .close = close_transport
    };
    *out = (DtTransport){.kind = DT_TRANSPORT_BLUETOOTH, .ops = &ops, .context = b};
    return 0;
}
