#include "../../include/proxy.h"

/* Blacklist/whitelist structures */
typedef struct pattern_node {
    char pattern[256];
    struct pattern_node *next;
} pattern_node_t;

static pattern_node_t *g_url_blacklist = NULL;
static pattern_node_t *g_url_whitelist = NULL;
static pattern_node_t *g_ip_blacklist = NULL;
static pattern_node_t *g_ip_whitelist = NULL;
static pthread_mutex_t g_filter_mutex = PTHREAD_MUTEX_INITIALIZER;

static int pattern_match(const char *pattern, const char *str) {
    /* Simple wildcard matching (* matches any sequence) */
    const char *p = pattern;
    const char *s = str;
    const char *star = NULL;
    const char *ss = s;

    while (*s) {
        if (*p == '*') {
            star = p++;
            ss = s;
        } else if (*p == *s || *p == '?') {
            p++;
            s++;
        } else if (star) {
            p = star + 1;
            s = ++ss;
        } else {
            return 0;
        }
    }

    while (*p == '*') {
        p++;
    }

    return *p == '\0';
}

static void add_pattern(pattern_node_t **list, const char *pattern) {
    pattern_node_t *node = malloc(sizeof(pattern_node_t));
    if (!node) {
        return;
    }

    strncpy(node->pattern, pattern, sizeof(node->pattern) - 1);
    node->next = *list;
    *list = node;
}

static int check_pattern_list(pattern_node_t *list, const char *str) {
    pattern_node_t *node = list;
    while (node) {
        if (pattern_match(node->pattern, str)) {
            return 1;
        }
        node = node->next;
    }
    return 0;
}

int filter_init(void) {
    /* Add some default blocked patterns */
    add_pattern(&g_url_blacklist, "*malware*");
    add_pattern(&g_url_blacklist, "*phishing*");
    add_pattern(&g_url_blacklist, "*tracking*");
    add_pattern(&g_url_blacklist, "*.ads.*");

    /* Add blocked IPs if configured */
    if (g_config.blacklist_count > 0) {
        for (int i = 0; i < g_config.blacklist_count; i++) {
            add_pattern(&g_ip_blacklist, g_config.blacklist_ips[i]);
        }
    }

    /* Add whitelisted IPs if configured */
    if (g_config.whitelist_count > 0) {
        for (int i = 0; i < g_config.whitelist_count; i++) {
            add_pattern(&g_ip_whitelist, g_config.whitelist_ips[i]);
        }
    }

    printf("[INFO] Content filtering initialized\n");
    return 0;
}

int filter_check_url(const char *url) {
    if (!url) {
        return -1;
    }

    pthread_mutex_lock(&g_filter_mutex);

    /* Check whitelist first - if whitelisted, allow */
    if (g_url_whitelist && check_pattern_list(g_url_whitelist, url)) {
        pthread_mutex_unlock(&g_filter_mutex);
        return 0;
    }

    /* Check blacklist - if blacklisted, block */
    if (g_url_blacklist && check_pattern_list(g_url_blacklist, url)) {
        pthread_mutex_unlock(&g_filter_mutex);
        return -1;
    }

    pthread_mutex_unlock(&g_filter_mutex);
    return 0;
}

int filter_check_ip(const char *ip) {
    if (!ip) {
        return -1;
    }

    pthread_mutex_lock(&g_filter_mutex);

    /* If whitelist exists and IP is not whitelisted, block */
    if (g_ip_whitelist) {
        if (!check_pattern_list(g_ip_whitelist, ip)) {
            pthread_mutex_unlock(&g_filter_mutex);
            return -1;
        }
        pthread_mutex_unlock(&g_filter_mutex);
        return 0;
    }

    /* Check blacklist - if blacklisted, block */
    if (g_ip_blacklist && check_pattern_list(g_ip_blacklist, ip)) {
        pthread_mutex_unlock(&g_filter_mutex);
        return -1;
    }

    pthread_mutex_unlock(&g_filter_mutex);
    return 0;
}

static void free_pattern_list(pattern_node_t *list) {
    while (list) {
        pattern_node_t *next = list->next;
        free(list);
        list = next;
    }
}

void filter_cleanup(void) {
    pthread_mutex_lock(&g_filter_mutex);

    free_pattern_list(g_url_blacklist);
    free_pattern_list(g_url_whitelist);
    free_pattern_list(g_ip_blacklist);
    free_pattern_list(g_ip_whitelist);

    g_url_blacklist = NULL;
    g_url_whitelist = NULL;
    g_ip_blacklist = NULL;
    g_ip_whitelist = NULL;

    pthread_mutex_unlock(&g_filter_mutex);

    printf("[INFO] Filtering system cleaned up\n");
}
