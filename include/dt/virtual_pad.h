#ifndef DT_VIRTUAL_PAD_H
#define DT_VIRTUAL_PAD_H
#include "dt/ps4_mapping.h"

typedef struct DtVirtualPad DtVirtualPad;
int dt_virtual_open(DtVirtualPad **out);
int dt_virtual_send(DtVirtualPad *pad, const ScePadData *sample);
enum { DT_VIRTUAL_POWER_OFF = 2 };
/* Returns 1 for user logout, DT_VIRTUAL_POWER_OFF for own native power request,
 * 0 to continue, negative on a log read failure. */
int dt_virtual_poll(DtVirtualPad *pad);
/* Neutralizes/deletes own device, restores privileges and frees the context. */
int dt_virtual_close(DtVirtualPad *pad);
#endif
