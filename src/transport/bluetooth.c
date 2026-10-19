#include "dt/bluetooth.h"
#include <stdlib.h>

typedef struct {
    DtBluetoothLinkOps link;
    void *context;
    DtBluetoothFraming framing;
    int acquired, ready;
} DtBluetooth;

static void release_link(DtBluetooth *b)
{
    if (b->acquired) b->link.release(b->context);
    b->acquired = b->ready = 0;
}

static int reconnect(void *context)
{
    DtBluetooth *b = context;
    release_link(b);
    b->acquired = 1; /* Includes partial failures: always release on failure. */
    int ret = b->link.acquire(b->context, 250);
    if (ret != 1) { release_link(b); return ret == 0 ? 0 : -1; }
    uint8_t command[6];
    size_t size = dt_ds3_bluetooth_enable(command, sizeof(command), DT_BT_HID_REPORT);
    ret = b->link.set_feature(b->context, command, size, 250);
    if (ret != (int)size) { release_link(b); return -1; }
    b->ready = 1;
    return 1;
}

static int read_report(void *context, DtInputReport *out, unsigned timeout_ms)
{
    DtBluetooth *b = context;
    if (!out || !timeout_ms || timeout_ms > 1000 || !b->ready) return -1;
    uint8_t data[129]; /* 128 canonical bytes plus a possible HIDP header. */
    size_t size = 0;
    uint64_t timestamp = 0;
    int ret = b->link.receive(b->context, data, sizeof(data), &size, &timestamp, timeout_ms);
    if (ret == 0) return 0;
    if (ret != 1 || size > sizeof(data) || !timestamp ||
        dt_ds3_bluetooth_unpack(data, size, b->framing, timestamp, out) != 0) {
        release_link(b); /* Neutralize/recover through the existing session. */
        return -1;
    }
    return 1;
}

static void close_transport(void *context)
{
    DtBluetooth *b = context;
    release_link(b);
    free(b);
}

int dt_bluetooth_create(DtTransport *out, const DtBluetoothLinkOps *link,
                        void *context, DtBluetoothFraming framing)
{
    if (!out || out->ops || !link || !link->acquire || !link->set_feature ||
        !link->receive || !link->release ||
        (framing != DT_BT_HID_REPORT && framing != DT_BT_HIDP_SDU)) return -1;
    DtBluetooth *b = calloc(1, sizeof(*b));
    if (!b) return -1;
    b->link = *link; b->context = context; b->framing = framing;
    static const DtTransportOps ops = {
        .read_report = read_report, .reconnect = reconnect, .close = close_transport
    };
    *out = (DtTransport){.kind = DT_TRANSPORT_BLUETOOTH, .ops = &ops, .context = b};
    return 0;
}
