#include "dt/controller.h"

int dt_controller_decode(const DtInputReport *report, DtControllerState *out)
{
    if (!report || !out) return -1;
    switch (report->format) {
    case DT_REPORT_DS3: return dt_ds3_decode(report, out);
    case DT_REPORT_DUALSENSE_USB:
    case DT_REPORT_DUALSENSE_BT: return dt_dualsense_decode(report, out);
    default: return -1;
    }
}
