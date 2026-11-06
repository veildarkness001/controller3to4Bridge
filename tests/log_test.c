#include "dt/log.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef DT_TEST_LOG_SYNC
static unsigned syncs,messages;
int dt_test_fsync(int fd) {assert(fd>=0);++syncs;return 0;}
#endif
void klog_printf(const char *format, ...)
{
    (void)format;
#ifdef DT_TEST_LOG_SYNC
    ++messages;assert(syncs>=messages); /* Sync the file before kernel output. */
#endif
}
int main(void)
{
    const char *current = "work/tests/logs/rotation.log";
    const char *previous = "work/tests/logs/rotation.log.1";
    assert(dt_log_open_named("../bad") < 0);
    assert(dt_log_open_named("rotation.log") == 0);
    char line[700]; memset(line, 'x', sizeof(line) - 1); line[699] = 0;
    for (int i = 0; i < 5000; ++i) dt_log("%d %s\n", i, line);
    dt_log("final cleanup success\n");
    dt_log_close();
    struct stat st;
    assert(stat(current, &st) == 0 && st.st_size <= 512 * 1024);
    assert(stat(previous, &st) == 0 && st.st_size <= 512 * 1024);
    FILE *f = fopen(current, "r"); assert(f);
    assert(fseek(f, -22, SEEK_END) == 0);
    char tail[64] = {0}; assert(fread(tail, 1, sizeof(tail) - 1, f) > 0); fclose(f);
    assert(strstr(tail, "final cleanup success"));
    assert(dt_log_open_named("rotation.log") == 0);
    assert(access(previous, F_OK) != 0); /* Never merge a previous run's log. */
    dt_log_close(); unlink(current);
    puts("PASS logging: repeated rotation stays below 1 MiB, cleanup retained and old run removed");
}
