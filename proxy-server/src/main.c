#include "../include/proxy.h"

/* Global variables */
proxy_config_t g_config;
volatile int g_running = 1;

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n[INFO] Received signal %d, shutting down gracefully...\n", signum);
        g_running = 0;
    }
}

void print_banner(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║         ProxyMax - Multithreaded Proxy Server           ║\n");
    printf("║              Full-Featured Edition v1.0                  ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

void print_config(proxy_config_t *config) {
    const char *mode_str[] = {"HTTP", "HTTPS", "SOCKS5", "TCP", "REVERSE", "TRANSPARENT"};
    const char *thread_model_str[] = {"PER-CONNECTION", "POOL", "EVENT-DRIVEN", "HYBRID"};

    printf("[CONFIG] Proxy Configuration:\n");
    printf("  ├─ Mode: %s\n", mode_str[config->mode]);
    printf("  ├─ Listen: %s:%d\n", config->bind_address, config->port);
    printf("  ├─ Threading: %s (%d threads)\n",
           thread_model_str[config->thread_model], config->num_threads);
    printf("  ├─ Max Connections: %d\n", config->max_connections);
    printf("  ├─ Features:\n");
    printf("  │   ├─ Caching: %s\n", config->enable_caching ? "✓" : "✗");
    printf("  │   ├─ SSL/TLS: %s\n", config->enable_ssl ? "✓" : "✗");
    printf("  │   ├─ Authentication: %s\n", config->enable_auth ? "✓" : "✗");
    printf("  │   ├─ Logging: %s\n", config->enable_logging ? "✓" : "✗");
    printf("  │   ├─ Filtering: %s\n", config->enable_filtering ? "✓" : "✗");
    printf("  │   ├─ Compression: %s\n", config->enable_compression ? "✓" : "✗");
    printf("  │   └─ Rate Limiting: %s\n", config->enable_rate_limiting ? "✓" : "✗");
    printf("  └─ Status: Ready\n\n");
}

void usage(const char *progname) {
    printf("Usage: %s [OPTIONS]\n\n", progname);
    printf("Options:\n");
    printf("  -c, --config FILE      Load configuration from FILE\n");
    printf("  -p, --port PORT        Listen port (default: 8080)\n");
    printf("  -b, --bind ADDR        Bind address (default: 0.0.0.0)\n");
    printf("  -m, --mode MODE        Proxy mode: http|https|socks5|tcp|reverse\n");
    printf("  -t, --threads NUM      Number of worker threads (default: 10)\n");
    printf("  -M, --thread-model M   Threading model: pool|event|hybrid|perconn\n");
    printf("  --enable-cache         Enable caching\n");
    printf("  --enable-ssl           Enable SSL/TLS support\n");
    printf("  --enable-auth          Enable authentication\n");
    printf("  --enable-logging       Enable logging\n");
    printf("  --enable-filtering     Enable content filtering\n");
    printf("  --enable-compression   Enable compression\n");
    printf("  --enable-rate-limit    Enable rate limiting\n");
    printf("  -h, --help             Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s -p 8080 -m http --enable-cache\n", progname);
    printf("  %s -c proxy.conf --enable-ssl --enable-auth\n", progname);
    printf("  %s -p 1080 -m socks5 -t 20 -M pool\n", progname);
    printf("\n");
}

int parse_args(int argc, char **argv, proxy_config_t *config) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return -1;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (++i < argc) {
                return config_load(argv[i], config);
            }
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (++i < argc) {
                config->port = atoi(argv[i]);
            }
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bind") == 0) {
            if (++i < argc) {
                strncpy(config->bind_address, argv[i], sizeof(config->bind_address) - 1);
            }
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mode") == 0) {
            if (++i < argc) {
                if (strcmp(argv[i], "http") == 0) config->mode = PROXY_MODE_HTTP;
                else if (strcmp(argv[i], "https") == 0) config->mode = PROXY_MODE_HTTPS;
                else if (strcmp(argv[i], "socks5") == 0) config->mode = PROXY_MODE_SOCKS5;
                else if (strcmp(argv[i], "tcp") == 0) config->mode = PROXY_MODE_TCP;
                else if (strcmp(argv[i], "reverse") == 0) config->mode = PROXY_MODE_REVERSE;
            }
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) {
            if (++i < argc) {
                config->num_threads = atoi(argv[i]);
            }
        } else if (strcmp(argv[i], "-M") == 0 || strcmp(argv[i], "--thread-model") == 0) {
            if (++i < argc) {
                if (strcmp(argv[i], "pool") == 0) config->thread_model = THREAD_MODEL_POOL;
                else if (strcmp(argv[i], "event") == 0) config->thread_model = THREAD_MODEL_EVENT_DRIVEN;
                else if (strcmp(argv[i], "hybrid") == 0) config->thread_model = THREAD_MODEL_HYBRID;
                else if (strcmp(argv[i], "perconn") == 0) config->thread_model = THREAD_MODEL_PER_CONNECTION;
            }
        } else if (strcmp(argv[i], "--enable-cache") == 0) {
            config->enable_caching = 1;
        } else if (strcmp(argv[i], "--enable-ssl") == 0) {
            config->enable_ssl = 1;
        } else if (strcmp(argv[i], "--enable-auth") == 0) {
            config->enable_auth = 1;
        } else if (strcmp(argv[i], "--enable-logging") == 0) {
            config->enable_logging = 1;
        } else if (strcmp(argv[i], "--enable-filtering") == 0) {
            config->enable_filtering = 1;
        } else if (strcmp(argv[i], "--enable-compression") == 0) {
            config->enable_compression = 1;
        } else if (strcmp(argv[i], "--enable-rate-limit") == 0) {
            config->enable_rate_limiting = 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    print_banner();

    /* Set default configuration */
    config_set_defaults(&g_config);

    /* Parse command line arguments */
    if (parse_args(argc, argv, &g_config) != 0) {
        return 1;
    }

    /* Print configuration */
    print_config(&g_config);

    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    /* Initialize subsystems */
    if (g_config.enable_logging) {
        logger_init(g_config.log_file, g_config.log_level);
    }

    if (g_config.enable_caching) {
        cache_init(g_config.cache_size_mb);
    }

    if (g_config.enable_auth) {
        auth_init(g_config.auth_file);
    }

    if (g_config.enable_filtering) {
        filter_init();
    }

    stats_init();

    /* Initialize and start proxy server */
    if (proxy_server_init(&g_config) != 0) {
        fprintf(stderr, "[ERROR] Failed to initialize proxy server\n");
        return 1;
    }

    printf("[INFO] Proxy server starting on %s:%d...\n",
           g_config.bind_address, g_config.port);

    if (proxy_server_start() != 0) {
        fprintf(stderr, "[ERROR] Failed to start proxy server\n");
        proxy_server_cleanup();
        return 1;
    }

    /* Cleanup */
    printf("\n[INFO] Performing cleanup...\n");
    proxy_server_cleanup();

    if (g_config.enable_caching) {
        cache_cleanup();
    }

    if (g_config.enable_auth) {
        auth_cleanup();
    }

    if (g_config.enable_filtering) {
        filter_cleanup();
    }

    if (g_config.enable_logging) {
        logger_cleanup();
    }

    printf("[INFO] Proxy server stopped successfully\n");
    return 0;
}
