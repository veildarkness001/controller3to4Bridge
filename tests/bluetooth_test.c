#include "dt/bluetooth.h"
#include "dt/bridge_session.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int acquired, released, features, result, feature_result, read_result;
    size_t size;
    uint8_t packet[130];
    uint64_t timestamp;
    unsigned timeout;
} Link;
static int acquire(void *context, unsigned timeout)
{
    Link *l = context; assert(timeout == 250); ++l->acquired; return l->result;
}
static int feature(void *context, const uint8_t *data, size_t size, unsigned timeout)
{
    Link *l = context;
    const uint8_t expected[] = {0xf4, 0x42, 3, 0, 0};
    assert(size == sizeof(expected) && !memcmp(data, expected, size) && timeout == 250);
    ++l->features; return l->feature_result;
}
static int receive(void *context, uint8_t *data, size_t capacity, size_t *size,
                   uint64_t *timestamp, unsigned timeout)
{
    Link *l = context; assert(capacity == 129); l->timeout = timeout;
    memcpy(data, l->packet, l->size > capacity ? capacity : l->size);
    *size = l->size; *timestamp = l->timestamp; return l->read_result;
}
static void release(void *context) { ++((Link *)context)->released; }
static const DtBluetoothLinkOps ops = {acquire, feature, receive, release};

static uint64_t now = 1;
static int session_mode, neutrals, recovered, touches, session_features;
uint64_t dt_now_us(void) { return now; }
int dt_test_usleep(unsigned us) { now += us; return 0; }
void dt_log(const char *format, ...) { (void)format; }
static int session_acquire(void *context, unsigned timeout)
{
    if (now >= 1000000 && now < 2000000) return 0;
    return acquire(context, timeout);
}
static int session_feature(void *context, const uint8_t *data, size_t size, unsigned timeout)
{
    ++session_features; return feature(context, data, size, timeout);
}
static int session_receive(void *context, uint8_t *data, size_t capacity,
                           size_t *size, uint64_t *timestamp, unsigned timeout)
{
    if (now >= 1000000 && now < 2000000) return -1;
    Link *l = context; l->timestamp = now;
    /* Hold Select across the loss, release before 3s, then press once more. */
    l->packet[3] = ((now > 20000 && now < 2500000) || now >= 3000000) ? 1 : 0;
    return receive(context, data, capacity, size, timestamp, timeout);
}
int dt_virtual_poll(DtVirtualPad *p) { (void)p; return 0; }
int dt_virtual_send(DtVirtualPad *p, const ScePadData *s)
{
    (void)p; assert(session_mode);
    if (now > 20000 && now < 1000000 && s->touchData.fingers) ++touches;
    if (now >= 1000000 && now < 2000000) {
        assert(!s->buttons && !s->touchData.fingers && s->rx == 128); ++neutrals;
    }
    if (now >= 2000000 && now < 3000000) {
        assert(!s->touchData.fingers && !(s->buttons & 0x100000)); ++recovered;
    }
    return 0;
}
static int stop(void *context) { (void)context; return now >= 3500000; }

int main(void)
{
    uint8_t packet[130] = {0xa1, 1};
    packet[4] = 0x40; packet[7] = 23;
    DtInputReport output = {.size = 7}, before = output;
    assert(dt_ds3_bluetooth_unpack(packet, 50, DT_BT_HIDP_SDU, 123, &output) == 0);
    DtControllerState state;
    assert(dt_ds3_decode(&output, &state) == 0);
    assert(state.buttons == (1u << 14) && state.lx == 23 && state.timestamp_us == 123);
    assert(output.size == 49 && output.data[0] == 1);
    assert(dt_ds3_bluetooth_unpack(packet + 1, 49, DT_BT_HID_REPORT, 456, &output) == 0);
    assert(output.timestamp_us == 456 && output.data[6] == 23);
    output = before;
    for (size_t size = 0; size < 50; ++size) {
        assert(dt_ds3_bluetooth_unpack(packet, size, DT_BT_HIDP_SDU, 1, &output) < 0);
        assert(!memcmp(&before, &output, sizeof(output)));
    }
    assert(dt_ds3_bluetooth_unpack(packet, 130, DT_BT_HIDP_SDU, 1, &output) < 0);
    for (unsigned header = 0; header <= 255; ++header) {
        packet[0] = header;
        if (header != 0xa1) assert(dt_ds3_bluetooth_unpack(packet, 50, DT_BT_HIDP_SDU, 1, &output) < 0);
    }
    packet[0] = 0xa1; packet[1] = 2;
    assert(dt_ds3_bluetooth_unpack(packet, 50, DT_BT_HIDP_SDU, 1, &output) < 0);
    packet[1] = 1; packet[2] = 0xff;
    assert(dt_ds3_bluetooth_unpack(packet, 50, DT_BT_HIDP_SDU, 1, &output) < 0);
    packet[2] = 0;
    assert(dt_ds3_bluetooth_unpack(NULL, 50, DT_BT_HIDP_SDU, 1, &output) < 0);
    assert(dt_ds3_bluetooth_unpack(packet, 50, 0, 1, &output) < 0);
    assert(!memcmp(&before, &output, sizeof(output)));
    uint8_t command[6] = {9,9,9,9,9,9};
    assert(dt_ds3_bluetooth_enable(command, 4, DT_BT_HID_REPORT) == 0 && command[0] == 9);
    assert(dt_ds3_bluetooth_enable(command, 6, DT_BT_HIDP_SDU) == 6);
    const uint8_t expected[] = {0x53,0xf4,0x42,3,0,0};
    assert(!memcmp(command, expected, 6));
    assert(dt_ds3_bluetooth_enable(NULL, 6, DT_BT_HIDP_SDU) == 0);
    assert(dt_ds3_bluetooth_enable(command, 6, 0) == 0);

    for (int framing = DT_BT_HID_REPORT; framing <= DT_BT_HIDP_SDU; ++framing) {
        Link link = {.result=1, .feature_result=5, .read_result=1, .timestamp=10};
        link.size = framing == DT_BT_HID_REPORT ? 49 : 50;
        memcpy(link.packet, packet + (framing == DT_BT_HID_REPORT), link.size);
        DtTransport t = {0};
        assert(dt_bluetooth_create(&t, &ops, &link, framing) == 0);
        assert(t.kind == DT_TRANSPORT_BLUETOOTH && !t.ops->set_feedback && link.acquired == 0);
        assert(dt_bluetooth_create(&t, &ops, &link, framing) < 0); /* No lost owner. */
        assert(t.ops->read_report(t.context, &output, 4) < 0);
        link.result = 0; assert(t.ops->reconnect(t.context) == 0 && link.released == 1);
        link.result = -1; assert(t.ops->reconnect(t.context) < 0 && link.released == 2);
        link.result = 1;
        for (int fail = -1; fail <= 4; ++fail) {
            link.feature_result = fail; assert(t.ops->reconnect(t.context) < 0);
        }
        link.feature_result = 5; assert(t.ops->reconnect(t.context) == 1);
        assert(t.ops->read_report(t.context, &output, 20) == 1 && link.timeout == 20);
        assert(output.size == 49 && output.timestamp_us == 10);
        assert(t.ops->read_report(t.context, &output, 0) < 0);
        assert(t.ops->read_report(t.context, &output, 1001) < 0);
        link.read_result = 0; output = before;
        assert(t.ops->read_report(t.context, &output, 4) == 0 && !memcmp(&output,&before,sizeof(output)));
        link.read_result = 1; link.size = 130;
        int releases = link.released;
        assert(t.ops->read_report(t.context, &output, 4) < 0 && link.released == releases + 1);
        assert(!memcmp(&output,&before,sizeof(output)));
        for (int cycle = 0; cycle < 100; ++cycle) assert(t.ops->reconnect(t.context) == 1);
        link.read_result = -1; releases = link.released;
        assert(t.ops->read_report(t.context, &output, 4) < 0 && link.released == releases+1);
        t.ops->close(t.context); assert(link.released == releases+1); /* No double release. */
    }
    DtTransport t = {0};
    assert(dt_bluetooth_create(&t, NULL, NULL, DT_BT_HID_REPORT) < 0);
    assert(dt_bluetooth_create(&t, &ops, NULL, 0) < 0);
    Link link = {.result=1, .feature_result=5, .read_result=1, .timestamp=1, .size=50};
    link.packet[0]=0xa1; link.packet[1]=1;
    link.packet[7]=link.packet[8]=link.packet[9]=link.packet[10]=128;
    DtBluetoothLinkOps session_ops = {session_acquire, session_feature, session_receive, release};
    assert(dt_bluetooth_create(&t, &session_ops, &link, DT_BT_HIDP_SDU) == 0);
    session_mode=1;
    DtBridgeConfig config = {.should_stop=stop};
    DtBridgeStats stats;
    assert(dt_bridge_run(&t, (DtVirtualPad *)1, &config, &stats) == 0);
    assert(stats.valid > 0 && stats.read_errors == 1 && stats.connections == 2);
    assert(session_features == 2 && neutrals && recovered && touches);
    t.ops->close(t.context);
    puts("PASS Bluetooth: framing, BT F4, failures, cleanup, repeated recovery and real session/mapping integration (simulated link)");
}
