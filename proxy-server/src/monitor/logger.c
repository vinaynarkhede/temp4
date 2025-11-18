#include "../../include/proxy.h"
#include <stdarg.h>

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3

static FILE *g_log_file = NULL;
static int g_log_level = LOG_LEVEL_INFO;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

void logger_init(const char *log_file, int log_level) {
    g_log_level = log_level;

    if (log_file && strlen(log_file) > 0) {
        g_log_file = fopen(log_file, "a");
        if (!g_log_file) {
            fprintf(stderr, "[WARN] Cannot open log file: %s, using stderr\n",
                    log_file);
            g_log_file = stderr;
        } else {
            printf("[INFO] Logging to file: %s\n", log_file);
        }
    } else {
        g_log_file = stderr;
    }
}

void logger_log(int level, const char *fmt, ...) {
    if (!g_log_file || level < g_log_level) {
        return;
    }

    const char *level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};

    pthread_mutex_lock(&g_log_mutex);

    /* Get current time */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    /* Print timestamp and level */
    fprintf(g_log_file, "[%s] [%s] ", time_buf, level_str[level]);

    /* Print message */
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log_file, fmt, args);
    va_end(args);

    fprintf(g_log_file, "\n");
    fflush(g_log_file);

    pthread_mutex_unlock(&g_log_mutex);
}

void logger_cleanup(void) {
    if (g_log_file && g_log_file != stderr) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}
