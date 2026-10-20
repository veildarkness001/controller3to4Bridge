#include "dt/auto_transport.h"
#include "dt/log.h"

static int reconnect(void *context)
{
    DtAutoTransport *s = context;
    s->active = NULL;
    DtTransport *choices[] = {s->usb, s->bluetooth};
    int usb_present = s->usb && s->usb_present ? s->usb_present(s->usb) : 0;
    if (usb_present > 0 && s->bluetooth && s->quiesce_bluetooth &&
        s->quiesce_bluetooth(s->bluetooth) != 0) return -1;
    for (unsigned i = 0; i < 2; ++i) {
        DtTransport *t = choices[i];
        if (!i && s->usb_present && usb_present <= 0) continue;
        if (t && t->ops->reconnect(t->context) > 0) {
            s->active = t;
            s->next_probe = dt_now_us() + 500000;
            dt_log("selected physical transport=%s\n", i ? "Bluetooth" : "USB");
            return 1;
        }
    }
    return 0;
}
static int read_report(void *context, DtInputReport *out, unsigned timeout_ms)
{
    DtAutoTransport *s = context;
    if (!s->active) return -1;
    uint64_t now = dt_now_us();
    if (s->active == s->bluetooth && s->usb && s->usb_present && now >= s->next_probe) {
        s->next_probe = now + 500000;
        if (s->usb_present(s->usb) > 0) {
            s->active = NULL;
            dt_log("USB attached; neutralize before transport change\n");
            return -1;
        }
    }
    int result = s->active->ops->read_report(s->active->context, out, timeout_ms);
    if (result < 0) s->active = NULL;
    return result;
}
static void close_transport(void *context)
{
    DtAutoTransport *s = context;
    s->active = NULL;
}
static const DtTransportOps ops = {
    .read_report = read_report, .reconnect = reconnect, .close = close_transport
};
static int valid(DtTransport *t)
{
    return t && t->ops && t->ops->reconnect && t->ops->read_report;
}
int dt_auto_transport_init(DtTransport *out, DtAutoTransport *s,
                          DtTransport *usb, DtTransport *bluetooth,
                          int (*usb_present)(DtTransport *usb),
                          int (*quiesce_bluetooth)(DtTransport *bluetooth))
{
    if (!out || !s || (!usb && !bluetooth) ||
        (usb && bluetooth && (!usb_present || !quiesce_bluetooth)) ||
        (usb && (!valid(usb) || usb->kind != DT_TRANSPORT_USB)) ||
        (bluetooth && (!valid(bluetooth) || bluetooth->kind != DT_TRANSPORT_BLUETOOTH)))
        return -1;
    *s = (DtAutoTransport){.usb = usb, .bluetooth = bluetooth, .usb_present = usb_present,
                          .quiesce_bluetooth = quiesce_bluetooth};
    *out = (DtTransport){.kind = DT_TRANSPORT_AUTO, .ops = &ops, .context = s};
    return 0;
}
