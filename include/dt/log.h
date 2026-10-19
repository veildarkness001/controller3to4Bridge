#ifndef DT_LOG_H
#define DT_LOG_H
#include <stdint.h>
int dt_log_open(void);
int dt_log_open_named(const char *name);
void dt_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
void dt_log_close(void);
uint64_t dt_now_us(void);
#endif
