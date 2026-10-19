#ifndef DT_VDA_EVENTS_H
#define DT_VDA_EVENTS_H
#include <stdint.h>

typedef struct {
    char marker[64];
    int marker_seen;
    uint64_t device_id;
    uint32_t user_id;
    int assigned;
} DtVdaEvents;

void dt_vda_event_line(DtVdaEvents *events, const char *line, int accept_add);
/* Diagnostic classification only: 1 own disconnect request, 2 own removal,
 * 3 another device's disconnect request, 0 unrelated/malformed. */
int dt_vda_menu_event(uint64_t own_device, const char *line);
#endif
