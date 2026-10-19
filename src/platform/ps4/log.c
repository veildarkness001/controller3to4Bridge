#include "dt/log.h"
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <ps4/klog.h>

static int log_fd = -1;
static char log_path[256], previous_path[260];
static size_t log_size;
#ifndef DT_LOG_DIRECTORY
#define DT_LOG_DIRECTORY "/data/dualshock_to_ps4"
#endif
#define LOG_LIMIT (512u * 1024u)

uint64_t dt_now_us(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

int dt_log_open(void)
{
    return dt_log_open_named("usb-diagnostic.log");
}

int dt_log_open_named(const char *name)
{
    if (!name || !*name || strchr(name, '/') || strstr(name, "..")) return -1;
    int n = snprintf(log_path, sizeof(log_path), "%s/%s", DT_LOG_DIRECTORY, name);
    if (n < 0 || (size_t)n >= sizeof(log_path)) return -1;
    snprintf(previous_path, sizeof(previous_path), "%s.1", log_path);
    dt_log_close();
    (void)mkdir(DT_LOG_DIRECTORY, 0777);
    log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (log_fd >= 0) { (void)unlink(previous_path); log_size = 0; }
    return log_fd < 0 ? -1 : 0;
}

void dt_log(const char *format, ...)
{
    char buf[768];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (n < 0) return;
    size_t size = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;
    klog_printf("[DualShockToPS4] %s", buf);
    if (log_fd >= 0 && log_size + size > LOG_LIMIT) {
        close(log_fd); log_fd = -1;
        /* At most two 512 KiB files, even during repeated cable failures. */
        if (rename(log_path, previous_path) == 0) {
            log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            log_size = 0;
        }
    }
    for (size_t offset = 0; log_fd >= 0 && offset < size;) {
        ssize_t written = write(log_fd, buf + offset, size - offset);
        if (written <= 0) break;
        log_size += (size_t)written;
        offset += (size_t)written;
    }
}

void dt_log_close(void)
{
    if (log_fd >= 0) { (void)fsync(log_fd); close(log_fd); log_fd = -1; }
}
