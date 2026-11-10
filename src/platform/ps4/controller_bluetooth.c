#include "dt/controller_bluetooth.h"
#include "dt/ps4_bluetooth.h"
#include "dt/ps4_dualsense_bluetooth.h"
#include "dt/log.h"
#include <stdlib.h>

int dt_controller_bluetooth_create(DtTransport *out,DtBluetoothController controller)
{
    if(!out || out->ops)return -1;
    if(controller==DT_BT_CONTROLLER_AUTO)return dt_controller_bluetooth_auto_create(out,controller);
    if(controller==DT_BT_CONTROLLER_DS3)return dt_ps4_bluetooth_create(out);
    if(controller==DT_BT_CONTROLLER_DUALSENSE)return dt_ps4_dualsense_bluetooth_create(out);
    return -1;
}
static int backend_status(const DtTransport *t)
{
    return dt_ps4_dualsense_bluetooth_matches(t)?dt_ps4_dualsense_bluetooth_status(t):dt_ps4_bluetooth_status(t);
}
static int backend_quiesce(DtTransport *t)
{
    return dt_ps4_dualsense_bluetooth_matches(t)?dt_ps4_dualsense_bluetooth_quiesce(t):dt_ps4_bluetooth_quiesce(t);
}
static int backend_destroy(DtTransport *t)
{
    return dt_ps4_dualsense_bluetooth_matches(t)?dt_ps4_dualsense_bluetooth_destroy(t):dt_ps4_bluetooth_destroy(t);
}

/* Discovery slots are not session limits. Once valid input is acquired the
 * model stays pinned until the app closes the activation (VDA first).
 * Never replace a native backend underneath an active virtual controller. */
#define DISCOVERY_US UINT64_C(10000000)
typedef struct {
    DtTransport backend;
    DtBluetoothController model;
    uint64_t since, previous;
    int fault, pinned, suspended, pending;
    DtInputReport first;
} AutomaticBluetooth;
static const DtTransportOps automatic_ops;
static DtBluetoothController last_model=DT_BT_CONTROLLER_DS3;
static int automatic_matches(const DtTransport *t)
{return t && t->ops==&automatic_ops && t->context;}
static int drop_backend(AutomaticBluetooth *a)
{
    if(a->backend.ops) {
        /* A fatal receive status remains fatal even when cleanup succeeds. */
        if(backend_status(&a->backend)!=0)a->fault=1;
        if(backend_destroy(&a->backend)!=0)a->fault=1;
    }
    return a->fault?-1:0;
}
static int start_backend(AutomaticBluetooth *a)
{
    int result=dt_controller_bluetooth_create(&a->backend,a->model);
    if(result==-2 || (result && a->backend.ops)) {a->fault=1;return -2;}
    if(result)return -1;
    a->since=a->previous=dt_now_us();
    if(!a->since) {a->fault=1;return -2;}
    dt_log("Bluetooth discovery listening for %s; idle slot=10s\n",
           a->model==DT_BT_CONTROLLER_DS3?"DS3":"DualSense");
    return 0;
}
static int check_clock(AutomaticBluetooth *a,uint64_t *now)
{
    *now=dt_now_us();
    if(!*now || *now<a->previous) {a->fault=1;return -1;}
    a->previous=*now;return 0;
}
static int automatic_reconnect(void *context)
{
    AutomaticBluetooth *a=context;
    if(a->fault || a->suspended)return -1;
    if(a->pinned)return a->pending?1:a->backend.ops->reconnect(a->backend.context);
    uint64_t now;
    if(check_clock(a,&now)!=0)return -1;
    /* Try input before expiring the idle slot, so an event at its boundary
     * wins. A successful reconnect promises a validated backend report. */
    if(a->backend.ops) {
        int ready=a->backend.ops->reconnect(a->backend.context);
        if(backend_status(&a->backend)!=0) {a->fault=1;return -1;}
        if(ready>0) {
            int result=a->backend.ops->read_report(a->backend.context,&a->first,20);
            if(result<0 || backend_status(&a->backend)!=0) {a->fault=1;return -1;}
            if(result>0) {
                if(a->first.format!=(a->model==DT_BT_CONTROLLER_DS3?DT_REPORT_DS3:DT_REPORT_DUALSENSE_BT)) {
                    a->fault=1;return -1;
                }
                a->pinned=a->pending=1;last_model=a->model;
                dt_log("Bluetooth automatic selection=%s; duration=unlimited\n",
                       a->model==DT_BT_CONTROLLER_DS3?"DS3":"DualSense");
                return 1;
            }
        }
    }
    if(check_clock(a,&now)!=0)return -1;
    if(now-a->since<DISCOVERY_US)return 0;
    if(drop_backend(a)!=0)return -1;
    a->model=a->model==DT_BT_CONTROLLER_DS3?DT_BT_CONTROLLER_DUALSENSE:DT_BT_CONTROLLER_DS3;
    int result=start_backend(a);
    if(result==-2)return -1;
    /* A safely unavailable model gets an idle slot too, rather than spinning
     * kernel setup on every poll. The other model remains eligible. */
    if(result) {a->since=now;dt_log("Bluetooth discovery model unavailable; next slot pending\n");}
    return 0;
}
static int automatic_read(void *context,DtInputReport *out,unsigned timeout)
{
    AutomaticBluetooth *a=context;
    if(!out || !timeout || timeout>1000 || a->fault || a->suspended || !a->pinned)return -1;
    if(a->pending) {*out=a->first;a->pending=0;return 1;}
    return a->backend.ops->read_report(a->backend.context,out,timeout);
}
static int automatic_cleanup(AutomaticBluetooth *a)
{
    int result=drop_backend(a);
    return result || a->fault ? -1 : 0;
}
static void automatic_close(void *context)
{AutomaticBluetooth *a=context;(void)automatic_cleanup(a);free(a);}
static const DtTransportOps automatic_ops={.reconnect=automatic_reconnect,
    .read_report=automatic_read,.close=automatic_close};

int dt_controller_bluetooth_auto_create(DtTransport *out,DtBluetoothController preferred)
{
    if(!out || out->ops || (preferred!=DT_BT_CONTROLLER_AUTO &&
       preferred!=DT_BT_CONTROLLER_DS3 && preferred!=DT_BT_CONTROLLER_DUALSENSE))return -1;
    AutomaticBluetooth *a=calloc(1,sizeof(*a));if(!a)return -1;
    a->model=preferred==DT_BT_CONTROLLER_AUTO?last_model:preferred;
    int result=start_backend(a);
    if(result==-1) {
        a->model=a->model==DT_BT_CONTROLLER_DS3?DT_BT_CONTROLLER_DUALSENSE:DT_BT_CONTROLLER_DS3;
        result=start_backend(a);
    }
    if(result) {int failed=automatic_cleanup(a);free(a);return failed?-2:-1;}
    *out=(DtTransport){.kind=DT_TRANSPORT_BLUETOOTH,.ops=&automatic_ops,.context=a};return 0;
}
int dt_controller_bluetooth_status(const DtTransport *t)
{
    if(!automatic_matches(t))return backend_status(t);
    const AutomaticBluetooth *a=t->context;
    return a->fault || (a->backend.ops && backend_status(&a->backend)!=0)?-1:0;
}
int dt_controller_bluetooth_quiesce(DtTransport *t)
{
    if(!automatic_matches(t))return backend_quiesce(t);
    AutomaticBluetooth *a=t->context;a->suspended=1;a->pending=0;
    /* Only used for the initial USB race; the app then unwinds the wrapper.
     * Destroy the listener now, and never start another while suspended. */
    return automatic_cleanup(a);
}
int dt_controller_bluetooth_destroy(DtTransport *t)
{
    if(!automatic_matches(t))return backend_destroy(t);
    AutomaticBluetooth *a=t->context;int result=automatic_cleanup(a);
    free(a);*t=(DtTransport){0};return result;
}
