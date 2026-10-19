#ifndef DT_PS4_MAPPING_H
#define DT_PS4_MAPPING_H
#include "dt/controller.h"
#include "c4f_sce.h"

void dt_ps4_neutral(ScePadData *out);
typedef struct {
    uint64_t previous_us, share_until_us;
    int64_t x, y; /* Coordinates in millionths of a touchpad pixel. */
    uint8_t held, used, blocked, finger, r3_consumed;
} DtPs4Mapping;

/* Zero-initialize before first use. On input loss cancel pending Share and
 * require Select release. */
void dt_ps4_mapping_reset(DtPs4Mapping *mapping);
/* Monotonic session time, independent of USB/BT report timestamps. */
void dt_ps4_map(DtPs4Mapping *mapping, const DtControllerState *state,
                uint64_t now_us, ScePadData *out);
#endif
