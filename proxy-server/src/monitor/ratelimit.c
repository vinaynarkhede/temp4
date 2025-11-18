#include "../../include/proxy.h"
#include <sys/time.h>

/* Token bucket for rate limiting */
typedef struct rate_limiter {
    char ip[INET_ADDRSTRLEN];
    double tokens;
    double max_tokens;
    double refill_rate;  /* tokens per second */
    time_t last_refill;
    struct rate_limiter *next;
} rate_limiter_t;

/* Bandwidth tracker */
typedef struct bandwidth_tracker {
    char ip[INET_ADDRSTRLEN];
    size_t bytes_sent;
    size_t bytes_received;
    time_t window_start;
    time_t last_activity;
    struct bandwidth_tracker *next;
} bandwidth_tracker_t;

static rate_limiter_t *g_rate_limiters = NULL;
static bandwidth_tracker_t *g_bandwidth_trackers = NULL;
static pthread_mutex_t g_ratelimit_mutex = PTHREAD_MUTEX_INITIALIZER;

static double get_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

/* Find or create rate limiter for IP */
static rate_limiter_t* get_rate_limiter(const char *ip) {
    rate_limiter_t *limiter = g_rate_limiters;

    /* Search for existing limiter */
    while (limiter) {
        if (strcmp(limiter->ip, ip) == 0) {
            return limiter;
        }
        limiter = limiter->next;
    }

    /* Create new limiter */
    limiter = malloc(sizeof(rate_limiter_t));
    if (!limiter) {
        return NULL;
    }

    strncpy(limiter->ip, ip, sizeof(limiter->ip) - 1);
    limiter->max_tokens = g_config.rate_limit_requests;
    limiter->tokens = limiter->max_tokens;
    limiter->refill_rate = g_config.rate_limit_requests / 60.0; /* per second */
    limiter->last_refill = time(NULL);
    limiter->next = g_rate_limiters;
    g_rate_limiters = limiter;

    return limiter;
}

/* Refill tokens based on elapsed time */
static void refill_tokens(rate_limiter_t *limiter) {
    time_t now = time(NULL);
    double elapsed = difftime(now, limiter->last_refill);

    if (elapsed > 0) {
        limiter->tokens += elapsed * limiter->refill_rate;
        if (limiter->tokens > limiter->max_tokens) {
            limiter->tokens = limiter->max_tokens;
        }
        limiter->last_refill = now;
    }
}

int ratelimit_check_request(const char *ip) {
    if (!g_config.enable_rate_limiting) {
        return 0; /* Rate limiting disabled */
    }

    pthread_mutex_lock(&g_ratelimit_mutex);

    rate_limiter_t *limiter = get_rate_limiter(ip);
    if (!limiter) {
        pthread_mutex_unlock(&g_ratelimit_mutex);
        return -1;
    }

    /* Refill tokens */
    refill_tokens(limiter);

    /* Check if request can be served */
    if (limiter->tokens >= 1.0) {
        limiter->tokens -= 1.0;
        pthread_mutex_unlock(&g_ratelimit_mutex);
        return 0; /* Allow request */
    }

    pthread_mutex_unlock(&g_ratelimit_mutex);
    return -1; /* Rate limit exceeded */
}

/* Get or create bandwidth tracker for IP */
static bandwidth_tracker_t* get_bandwidth_tracker(const char *ip) {
    bandwidth_tracker_t *tracker = g_bandwidth_trackers;
    time_t now = time(NULL);

    /* Search for existing tracker */
    while (tracker) {
        if (strcmp(tracker->ip, ip) == 0) {
            /* Reset window if expired (60 second window) */
            if (difftime(now, tracker->window_start) >= 60) {
                tracker->bytes_sent = 0;
                tracker->bytes_received = 0;
                tracker->window_start = now;
            }
            tracker->last_activity = now;
            return tracker;
        }
        tracker = tracker->next;
    }

    /* Create new tracker */
    tracker = malloc(sizeof(bandwidth_tracker_t));
    if (!tracker) {
        return NULL;
    }

    strncpy(tracker->ip, ip, sizeof(tracker->ip) - 1);
    tracker->bytes_sent = 0;
    tracker->bytes_received = 0;
    tracker->window_start = now;
    tracker->last_activity = now;
    tracker->next = g_bandwidth_trackers;
    g_bandwidth_trackers = tracker;

    return tracker;
}

int ratelimit_check_bandwidth(const char *ip, size_t bytes, int direction) {
    if (!g_config.enable_rate_limiting || g_config.rate_limit_bandwidth_kbps == 0) {
        return 0; /* Bandwidth limiting disabled */
    }

    pthread_mutex_lock(&g_ratelimit_mutex);

    bandwidth_tracker_t *tracker = get_bandwidth_tracker(ip);
    if (!tracker) {
        pthread_mutex_unlock(&g_ratelimit_mutex);
        return -1;
    }

    /* Calculate current bandwidth (bytes per second) */
    time_t now = time(NULL);
    double window_duration = difftime(now, tracker->window_start);
    if (window_duration == 0) {
        window_duration = 1;
    }

    size_t total_bytes = tracker->bytes_sent + tracker->bytes_received;
    size_t bandwidth_bps = total_bytes / window_duration;
    size_t limit_bps = g_config.rate_limit_bandwidth_kbps * 1024;

    /* Check if adding this transfer would exceed limit */
    if (bandwidth_bps + bytes > limit_bps) {
        pthread_mutex_unlock(&g_ratelimit_mutex);
        return -1; /* Bandwidth limit would be exceeded */
    }

    /* Update tracker */
    if (direction == 0) { /* upload */
        tracker->bytes_sent += bytes;
    } else { /* download */
        tracker->bytes_received += bytes;
    }

    pthread_mutex_unlock(&g_ratelimit_mutex);
    return 0;
}

void ratelimit_update_bandwidth(const char *ip, size_t bytes, int direction) {
    if (!g_config.enable_rate_limiting) {
        return;
    }

    pthread_mutex_lock(&g_ratelimit_mutex);

    bandwidth_tracker_t *tracker = get_bandwidth_tracker(ip);
    if (tracker) {
        if (direction == 0) { /* upload */
            tracker->bytes_sent += bytes;
        } else { /* download */
            tracker->bytes_received += bytes;
        }
    }

    pthread_mutex_unlock(&g_ratelimit_mutex);
}

/* Cleanup old trackers (called periodically) */
void ratelimit_cleanup_old_entries(void) {
    pthread_mutex_lock(&g_ratelimit_mutex);

    time_t now = time(NULL);
    bandwidth_tracker_t *tracker = g_bandwidth_trackers;
    bandwidth_tracker_t *prev = NULL;

    while (tracker) {
        bandwidth_tracker_t *next = tracker->next;

        /* Remove trackers inactive for > 5 minutes */
        if (difftime(now, tracker->last_activity) > 300) {
            if (prev) {
                prev->next = next;
            } else {
                g_bandwidth_trackers = next;
            }
            free(tracker);
        } else {
            prev = tracker;
        }

        tracker = next;
    }

    pthread_mutex_unlock(&g_ratelimit_mutex);
}

void ratelimit_init(void) {
    g_rate_limiters = NULL;
    g_bandwidth_trackers = NULL;

    if (g_config.enable_rate_limiting) {
        printf("[INFO] Rate limiting enabled: %d req/min, %d KB/s\n",
               g_config.rate_limit_requests,
               g_config.rate_limit_bandwidth_kbps);
    }
}

void ratelimit_cleanup(void) {
    pthread_mutex_lock(&g_ratelimit_mutex);

    /* Free rate limiters */
    rate_limiter_t *limiter = g_rate_limiters;
    while (limiter) {
        rate_limiter_t *next = limiter->next;
        free(limiter);
        limiter = next;
    }
    g_rate_limiters = NULL;

    /* Free bandwidth trackers */
    bandwidth_tracker_t *tracker = g_bandwidth_trackers;
    while (tracker) {
        bandwidth_tracker_t *next = tracker->next;
        free(tracker);
        tracker = next;
    }
    g_bandwidth_trackers = NULL;

    pthread_mutex_unlock(&g_ratelimit_mutex);

    printf("[INFO] Rate limiting system cleaned up\n");
}

void ratelimit_print_stats(void) {
    pthread_mutex_lock(&g_ratelimit_mutex);

    int num_limiters = 0;
    int num_trackers = 0;

    rate_limiter_t *limiter = g_rate_limiters;
    while (limiter) {
        num_limiters++;
        limiter = limiter->next;
    }

    bandwidth_tracker_t *tracker = g_bandwidth_trackers;
    while (tracker) {
        num_trackers++;
        tracker = tracker->next;
    }

    printf("[INFO] Rate Limiting Stats: %d IPs tracked, %d bandwidth monitors\n",
           num_limiters, num_trackers);

    pthread_mutex_unlock(&g_ratelimit_mutex);
}
