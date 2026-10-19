#ifndef DT_VIRTUAL_PAD_H
#define DT_VIRTUAL_PAD_H
#include "dt/ps4_mapping.h"

typedef struct DtVirtualPad DtVirtualPad;
int dt_virtual_open(DtVirtualPad **out);
int dt_virtual_send(DtVirtualPad *pad, const ScePadData *sample);
/* Returns 1 for user logout, 0 to continue, negative on a log read failure. */
int dt_virtual_poll(DtVirtualPad *pad);
/* Neutralizes/deletes own device, restores privileges and frees the context. */
int dt_virtual_close(DtVirtualPad *pad);
#endif
