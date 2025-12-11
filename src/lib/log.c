#include "hed.h"
#include <time.h>

static FILE *g_log_fp = NULL;
static char g_log_path[512] = {0};

static void log_vmsg(const char *fmt, va_list ap) {
    if (!g_log_fp)
        return;
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
    fprintf(g_log_fp, "[%s] ", ts);
    vfprintf(g_log_fp, fmt, ap);
    fputc('\n', g_log_fp);
    fflush(g_log_fp);
}

void log_init(const char *path) {
    if (g_log_fp)
        return;
    const char *p = path ? path : ".hedlog";
    snprintf(g_log_path, sizeof(g_log_path), "%s", p);
    g_log_fp = fopen(g_log_path, "a");
    if (!g_log_fp)
        return;
    setvbuf(g_log_fp, NULL, _IOLBF, 0);
}

void log_msg(const char *fmt, ...) {
    char msg[512];
    va_list ap;

    /* Format the message */
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    /* Write to log file with timestamp */
    va_start(ap, fmt);
    log_vmsg(fmt, ap);
    va_end(ap);
}

void log_clear(void) {
    if (!g_log_fp)
        return;
    fclose(g_log_fp);
    g_log_fp = NULL;
    g_log_fp = fopen(g_log_path[0] ? g_log_path : ".hedlog", "w");
    if (!g_log_fp)
        return;
    fclose(g_log_fp);
    g_log_fp = NULL;
    g_log_fp = fopen(g_log_path[0] ? g_log_path : ".hedlog", "a");
    if (g_log_fp)
        setvbuf(g_log_fp, NULL, _IOLBF, 0);
}

void log_close(void) {
    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
}
