#include "dt/bridge_session.h"
#include "dt/log.h"
#include <string.h>
#include <unistd.h>
#define REPORT_US UINT64_C(4000)
#define NEUTRAL_US UINT64_C(250000)
#define STALE_US UINT64_C(2000000)
#define RETRY_US UINT64_C(500000)
#define MAX_RETRY_US UINT64_C(5000000)
#define HEARTBEAT_US UINT64_C(60000000)

int dt_bridge_run(DtTransport *t, DtVirtualPad *pad,
                  const DtBridgeConfig *config, DtBridgeStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    uint64_t start = dt_now_us(), previous = start, last_input = 0, connected_at = 0;
    uint64_t retry_at = start, retry_delay = RETRY_US, next_report = start;
    uint64_t next_log = start + HEARTBEAT_US;
    if (!start || !t->ops || !t->ops->reconnect) return 1;
    ScePadData sample;
    DtControllerState state = {0};
    DtPs4Mapping mapping = {0};
    dt_ps4_mapping_reset(&mapping);
    dt_ps4_neutral(&sample);
    int connected = 0, has_input = 0;
    for (;;) {
        uint64_t now = dt_now_us();
        if (!now || now < previous) { dt_log("session clock failure\n"); return 1; }
        previous = now;
        stats->elapsed_us = now - start;
        if (config->should_stop && config->should_stop(config->context)) {
            dt_log("session stop requested\n"); return 0;
        }
        if (config->max_duration_us && now - start >= config->max_duration_us) {
            dt_log("session time limit reached\n"); return 0;
        }
        int ret = dt_virtual_poll(pad);
        if (ret != 0) return ret < 0 ? 3 : 0;
        if (!connected && now >= retry_at) {
            ret = t->ops->reconnect(t->context);
            now = dt_now_us();
            if (!now || now < previous) return 1;
            if (ret > 0) {
                connected = 1; has_input = 0; connected_at = now;
                last_input = 0; retry_delay = RETRY_US;
                ++stats->connections;
                dt_log("physical connected connection=%llu; waiting for valid input\n",
                       (unsigned long long)stats->connections);
            } else {
                retry_at = now + retry_delay;
                if (retry_delay < MAX_RETRY_US) {
                    retry_delay *= 2;
                    if (retry_delay > MAX_RETRY_US) retry_delay = MAX_RETRY_US;
                }
            }
        }
        int force_neutral = 0;
        if (connected) {
            DtInputReport report = {0};
            /* Allow a full input period while acquiring/recovering the stream.
             * Once flowing, keep the existing short read for output scheduling.
             * This is a bounded recovery experiment, not a measured latency. */
            unsigned timeout = !has_input || now - last_input >= NEUTRAL_US ? 20 : 4;
            ret = t->ops->read_report(t->context, &report, timeout);
            now = dt_now_us();
            if (!now || now < previous) return 1;
            if (ret < 0) {
                ++stats->read_errors; connected = 0; force_neutral = 1;
                dt_log("physical input lost; keeping virtual device and retrying\n");
            } else if (ret > 0) {
                if (dt_ds3_decode(&report, &state) == 0) {
                    ++stats->valid; last_input = now; has_input = 1;
                } else ++stats->invalid;
            }
            if (connected && now - (has_input ? last_input : connected_at) >= STALE_US) {
                ++stats->stale; connected = 0; force_neutral = 1;
                dt_log("physical no valid input for 2s; reopening physical device\n");
            }
            if (!connected) {
                has_input = 0; retry_at = now + RETRY_US; retry_delay = RETRY_US;
            }
        }
        if (!connected || !has_input || now - last_input >= NEUTRAL_US) {
            dt_ps4_mapping_reset(&mapping);
            dt_ps4_neutral(&sample);
        } else dt_ps4_map(&mapping, &state, now, &sample);
        if (force_neutral || now >= next_report) {
            ret = dt_virtual_send(pad, &sample);
            if (ret != 0) {
                dt_log("InsertData failed: 0x%08x\n", (uint32_t)ret); return 4;
            }
            ++stats->sent; next_report = now + REPORT_US;
        }
        if (now >= next_log) {
            dt_log("session heartbeat elapsed_s=%llu connected=%d valid=%llu invalid=%llu sent=%llu connections=%llu read_errors=%llu stale=%llu\n",
                   (unsigned long long)((now - start) / 1000000), connected,
                   (unsigned long long)stats->valid, (unsigned long long)stats->invalid,
                   (unsigned long long)stats->sent, (unsigned long long)stats->connections,
                   (unsigned long long)stats->read_errors, (unsigned long long)stats->stale);
            next_log = now + HEARTBEAT_US;
        }
        /* Bound CPU use even for immediately returning USB reads. */
        usleep(1000);
    }
}
