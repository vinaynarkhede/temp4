#include "../../include/proxy.h"

typedef struct {
    unsigned long total_connections;
    unsigned long active_connections;
    unsigned long total_requests;
    unsigned long bytes_transferred;
    time_t start_time;
    pthread_mutex_t mutex;
} stats_t;

static stats_t g_stats;

void stats_init(void) {
    memset(&g_stats, 0, sizeof(stats_t));
    g_stats.start_time = time(NULL);
    pthread_mutex_init(&g_stats.mutex, NULL);
}

void stats_increment_connections(void) {
    pthread_mutex_lock(&g_stats.mutex);
    g_stats.total_connections++;
    g_stats.active_connections++;
    pthread_mutex_unlock(&g_stats.mutex);
}

void stats_decrement_connections(void) {
    pthread_mutex_lock(&g_stats.mutex);
    if (g_stats.active_connections > 0) {
        g_stats.active_connections--;
    }
    pthread_mutex_unlock(&g_stats.mutex);
}

void stats_increment_requests(void) {
    pthread_mutex_lock(&g_stats.mutex);
    g_stats.total_requests++;
    pthread_mutex_unlock(&g_stats.mutex);
}

void stats_add_bytes_transferred(size_t bytes) {
    pthread_mutex_lock(&g_stats.mutex);
    g_stats.bytes_transferred += bytes;
    pthread_mutex_unlock(&g_stats.mutex);
}

void stats_print(void) {
    pthread_mutex_lock(&g_stats.mutex);

    time_t now = time(NULL);
    time_t uptime = now - g_stats.start_time;

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║                    Server Statistics                     ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║ Total Connections:    %-34lu ║\n", g_stats.total_connections);
    printf("║ Active Connections:   %-34lu ║\n", g_stats.active_connections);
    printf("║ Total Requests:       %-34lu ║\n", g_stats.total_requests);
    printf("║ Bytes Transferred:    %-34lu ║\n", g_stats.bytes_transferred);

    /* Format bytes in human readable format */
    double bytes = (double)g_stats.bytes_transferred;
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    while (bytes >= 1024.0 && unit < 4) {
        bytes /= 1024.0;
        unit++;
    }
    printf("║ Data Transferred:     %-27.2f %s ║\n", bytes, units[unit]);

    /* Calculate uptime */
    int days = uptime / 86400;
    int hours = (uptime % 86400) / 3600;
    int minutes = (uptime % 3600) / 60;
    int seconds = uptime % 60;
    printf("║ Uptime:               %dd %02dh %02dm %02ds              ║\n",
           days, hours, minutes, seconds);

    printf("╚══════════════════════════════════════════════════════════╝\n");

    pthread_mutex_unlock(&g_stats.mutex);
}
