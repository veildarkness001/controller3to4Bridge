#ifndef DT_BRIDGE_SESSION_H
#define DT_BRIDGE_SESSION_H
#include "dt/controller.h"
#include "dt/virtual_pad.h"
typedef struct {
    uint64_t max_duration_us; /* 0: manual stop/logout/error, no time limit. */
    int (*should_stop)(void *context);
    void *context;
} DtBridgeConfig;
typedef struct {
    uint64_t valid, invalid, sent, connections, read_errors, stale, elapsed_us;
} DtBridgeStats;
/* One virtual device throughout physical recovery. Caller owns cleanup. */
int dt_bridge_run(DtTransport *transport, DtVirtualPad *pad,
                  const DtBridgeConfig *config, DtBridgeStats *stats);
#endif
