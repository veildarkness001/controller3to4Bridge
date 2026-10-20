#ifndef DT_PS4_BLUETOOTH_H
#define DT_PS4_BLUETOOTH_H
#include "dt/controller.h"
/* Experimental13.52 native backend. Requires a previously paired DS3 and
 * caller-owned lifetime. Open starts listening; it does not wait for PS.
 * Close must follow virtual-pad cleanup so credential snapshots unwind in order.
 * No USB calls, address writes, module loading or AutoRun. */
int dt_ps4_bluetooth_create(DtTransport *out);
/* create: -1 unavailable with successful rollback; -2 rollback failed.
 * Service drains native events while another transport supplies input. */
int dt_ps4_bluetooth_service(DtTransport *transport, unsigned timeout_ms);
/* Disconnect only the known DS3, verify link absence, discard pending events
 * and clear its stream state before USB activation. Retains listener/caps. */
int dt_ps4_bluetooth_quiesce(DtTransport *transport);
int dt_ps4_bluetooth_destroy(DtTransport *transport);
/* A malformed stream/platform failure is fatal; ordinary radio loss is not. */
int dt_ps4_bluetooth_status(const DtTransport *transport);
#endif
