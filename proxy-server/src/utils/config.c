#include "../../include/proxy.h"

void config_set_defaults(proxy_config_t *config) {
    memset(config, 0, sizeof(proxy_config_t));

    /* Network settings */
    config->port = 8080;
    strncpy(config->bind_address, "0.0.0.0", sizeof(config->bind_address) - 1);

    /* Proxy mode */
    config->mode = PROXY_MODE_HTTP;
    config->thread_model = THREAD_MODEL_POOL;

    /* Threading */
    config->num_threads = 10;
    config->max_connections = 1000;

    /* Feature flags - disabled by default */
    config->enable_caching = 0;
    config->enable_ssl = 0;
    config->enable_auth = 0;
    config->enable_logging = 1; /* Enable logging by default */
    config->enable_filtering = 0;
    config->enable_compression = 0;
    config->enable_rate_limiting = 0;

    /* Cache settings */
    config->cache_size_mb = 100;
    config->cache_ttl_seconds = 3600;

    /* SSL settings */
    strncpy(config->ssl_cert_file, "certs/server.crt", sizeof(config->ssl_cert_file) - 1);
    strncpy(config->ssl_key_file, "certs/server.key", sizeof(config->ssl_key_file) - 1);

    /* Auth settings */
    strncpy(config->auth_file, "config/users.txt", sizeof(config->auth_file) - 1);

    /* Reverse proxy */
    strncpy(config->backend_host, "localhost", sizeof(config->backend_host) - 1);
    config->backend_port = 80;
    config->num_backends = 1;

    /* Rate limiting */
    config->rate_limit_requests = 100; /* per minute */
    config->rate_limit_bandwidth_kbps = 10240; /* 10 Mbps */

    /* Logging */
    strncpy(config->log_file, "proxy.log", sizeof(config->log_file) - 1);
    config->log_level = 1; /* INFO */

    /* ACL */
    config->whitelist_ips = NULL;
    config->blacklist_ips = NULL;
    config->whitelist_count = 0;
    config->blacklist_count = 0;
}

int config_load(const char *config_file, proxy_config_t *config) {
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        fprintf(stderr, "[ERROR] Cannot open config file: %s\n", config_file);
        return -1;
    }

    char line[1024];
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;

        /* Remove newline */
        line[strcspn(line, "\n")] = 0;

        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\0' || line[0] == '\n') {
            continue;
        }

        /* Parse key=value pairs */
        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }

        *eq = '\0';
        char *key = line;
        char *value = eq + 1;

        /* Trim whitespace */
        while (*key == ' ' || *key == '\t') key++;
        while (*value == ' ' || *value == '\t') value++;

        /* Parse configuration */
        if (strcmp(key, "port") == 0) {
            config->port = atoi(value);
        } else if (strcmp(key, "bind_address") == 0) {
            strncpy(config->bind_address, value, sizeof(config->bind_address) - 1);
        } else if (strcmp(key, "mode") == 0) {
            if (strcmp(value, "http") == 0) config->mode = PROXY_MODE_HTTP;
            else if (strcmp(value, "https") == 0) config->mode = PROXY_MODE_HTTPS;
            else if (strcmp(value, "socks5") == 0) config->mode = PROXY_MODE_SOCKS5;
            else if (strcmp(value, "tcp") == 0) config->mode = PROXY_MODE_TCP;
            else if (strcmp(value, "reverse") == 0) config->mode = PROXY_MODE_REVERSE;
        } else if (strcmp(key, "num_threads") == 0) {
            config->num_threads = atoi(value);
        } else if (strcmp(key, "thread_model") == 0) {
            if (strcmp(value, "pool") == 0) config->thread_model = THREAD_MODEL_POOL;
            else if (strcmp(value, "event") == 0) config->thread_model = THREAD_MODEL_EVENT_DRIVEN;
            else if (strcmp(value, "hybrid") == 0) config->thread_model = THREAD_MODEL_HYBRID;
            else if (strcmp(value, "perconn") == 0) config->thread_model = THREAD_MODEL_PER_CONNECTION;
        } else if (strcmp(key, "enable_caching") == 0) {
            config->enable_caching = atoi(value);
        } else if (strcmp(key, "enable_ssl") == 0) {
            config->enable_ssl = atoi(value);
        } else if (strcmp(key, "enable_auth") == 0) {
            config->enable_auth = atoi(value);
        } else if (strcmp(key, "enable_logging") == 0) {
            config->enable_logging = atoi(value);
        } else if (strcmp(key, "enable_filtering") == 0) {
            config->enable_filtering = atoi(value);
        } else if (strcmp(key, "enable_compression") == 0) {
            config->enable_compression = atoi(value);
        } else if (strcmp(key, "enable_rate_limiting") == 0) {
            config->enable_rate_limiting = atoi(value);
        } else if (strcmp(key, "cache_size_mb") == 0) {
            config->cache_size_mb = atoi(value);
        } else if (strcmp(key, "ssl_cert") == 0) {
            strncpy(config->ssl_cert_file, value, sizeof(config->ssl_cert_file) - 1);
        } else if (strcmp(key, "ssl_key") == 0) {
            strncpy(config->ssl_key_file, value, sizeof(config->ssl_key_file) - 1);
        } else if (strcmp(key, "backend_host") == 0) {
            strncpy(config->backend_host, value, sizeof(config->backend_host) - 1);
        } else if (strcmp(key, "backend_port") == 0) {
            config->backend_port = atoi(value);
        } else if (strcmp(key, "log_file") == 0) {
            strncpy(config->log_file, value, sizeof(config->log_file) - 1);
        }
    }

    fclose(fp);
    printf("[INFO] Configuration loaded from %s\n", config_file);
    return 0;
}
