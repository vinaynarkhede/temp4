#include "../../include/proxy.h"
#include <limits.h>

/* Backend server structure */
typedef struct backend_server {
    char host[MAX_HOSTNAME];
    int port;
    int weight;
    int active_connections;
    int total_requests;
    int failed_requests;
    int is_healthy;
    time_t last_health_check;
    struct backend_server *next;
} backend_server_t;

/* Load balancer structure */
typedef struct {
    backend_server_t *servers;
    int num_servers;
    int current_index;  /* For round-robin */
    lb_algorithm_t algorithm;
    pthread_mutex_t mutex;
} load_balancer_t;

static load_balancer_t *g_lb = NULL;

int loadbalancer_init(void) {
    if (g_config.mode != PROXY_MODE_REVERSE) {
        return 0; /* Load balancing only for reverse proxy */
    }

    g_lb = malloc(sizeof(load_balancer_t));
    if (!g_lb) {
        return -1;
    }

    memset(g_lb, 0, sizeof(load_balancer_t));
    g_lb->algorithm = g_config.lb_algorithm;
    pthread_mutex_init(&g_lb->mutex, NULL);

    /* Add backend servers from configuration */
    if (g_config.num_backends > 0) {
        for (int i = 0; i < g_config.num_backends; i++) {
            backend_server_t *server = malloc(sizeof(backend_server_t));
            if (!server) {
                continue;
            }

            memset(server, 0, sizeof(backend_server_t));
            strncpy(server->host, g_config.backend_hosts[i],
                    sizeof(server->host) - 1);
            server->port = g_config.backend_ports[i];
            server->weight = g_config.backend_weights ?
                            g_config.backend_weights[i] : 1;
            server->is_healthy = 1;
            server->last_health_check = time(NULL);

            /* Add to list */
            server->next = g_lb->servers;
            g_lb->servers = server;
            g_lb->num_servers++;
        }
    } else {
        /* Add single default backend */
        backend_server_t *server = malloc(sizeof(backend_server_t));
        if (server) {
            memset(server, 0, sizeof(backend_server_t));
            strncpy(server->host, g_config.backend_host,
                    sizeof(server->host) - 1);
            server->port = g_config.backend_port;
            server->weight = 1;
            server->is_healthy = 1;
            server->last_health_check = time(NULL);
            server->next = NULL;

            g_lb->servers = server;
            g_lb->num_servers = 1;
        }
    }

    const char *algo_names[] = {"Round-Robin", "Least-Connections", "IP-Hash", "Weighted-RR"};
    printf("[INFO] Load balancer initialized: %d backend(s), algorithm: %s\n",
           g_lb->num_servers, algo_names[g_lb->algorithm]);

    return 0;
}

/* Round-robin selection */
static backend_server_t* lb_select_round_robin(void) {
    backend_server_t *server = g_lb->servers;
    int count = 0;
    int target = g_lb->current_index % g_lb->num_servers;

    while (server && count < target) {
        server = server->next;
        count++;
        if (!server) {
            server = g_lb->servers;
        }
    }

    g_lb->current_index++;

    /* Skip unhealthy servers */
    int attempts = 0;
    while (server && !server->is_healthy && attempts < g_lb->num_servers) {
        server = server->next ? server->next : g_lb->servers;
        g_lb->current_index++;
        attempts++;
    }

    return server;
}

/* Least connections selection */
static backend_server_t* lb_select_least_connections(void) {
    backend_server_t *server = g_lb->servers;
    backend_server_t *selected = NULL;
    int min_connections = INT_MAX;

    while (server) {
        if (server->is_healthy && server->active_connections < min_connections) {
            min_connections = server->active_connections;
            selected = server;
        }
        server = server->next;
    }

    return selected;
}

/* IP hash selection (consistent hashing) */
static backend_server_t* lb_select_ip_hash(const char *client_ip) {
    /* Simple hash function */
    unsigned int hash = 5381;
    const char *p = client_ip;
    while (*p) {
        hash = ((hash << 5) + hash) + *p;
        p++;
    }

    int index = hash % g_lb->num_servers;
    backend_server_t *server = g_lb->servers;
    int count = 0;

    while (server && count < index) {
        server = server->next;
        count++;
    }

    /* If selected server is unhealthy, fall back to round-robin */
    if (server && !server->is_healthy) {
        server = lb_select_round_robin();
    }

    return server;
}

/* Weighted round-robin selection */
static backend_server_t* lb_select_weighted_round_robin(void) {
    backend_server_t *server = g_lb->servers;
    backend_server_t *selected = NULL;
    int total_weight = 0;
    int current_weight = 0;

    /* Calculate total weight */
    while (server) {
        if (server->is_healthy) {
            total_weight += server->weight;
        }
        server = server->next;
    }

    if (total_weight == 0) {
        return NULL;
    }

    /* Select based on weight */
    int target = g_lb->current_index % total_weight;
    server = g_lb->servers;

    while (server) {
        if (server->is_healthy) {
            current_weight += server->weight;
            if (current_weight > target) {
                selected = server;
                break;
            }
        }
        server = server->next;
    }

    g_lb->current_index++;
    return selected;
}

backend_server_t* loadbalancer_select_backend(const char *client_ip) {
    if (!g_lb) {
        return NULL;
    }

    pthread_mutex_lock(&g_lb->mutex);

    backend_server_t *server = NULL;

    switch (g_lb->algorithm) {
        case LB_ALGORITHM_ROUND_ROBIN:
            server = lb_select_round_robin();
            break;

        case LB_ALGORITHM_LEAST_CONNECTIONS:
            server = lb_select_least_connections();
            break;

        case LB_ALGORITHM_IP_HASH:
            server = lb_select_ip_hash(client_ip);
            break;

        case LB_ALGORITHM_WEIGHTED_RR:
            server = lb_select_weighted_round_robin();
            break;

        default:
            server = lb_select_round_robin();
            break;
    }

    if (server) {
        server->active_connections++;
        server->total_requests++;
    }

    pthread_mutex_unlock(&g_lb->mutex);
    return server;
}

void loadbalancer_release_backend(backend_server_t *server) {
    if (!g_lb || !server) {
        return;
    }

    pthread_mutex_lock(&g_lb->mutex);

    if (server->active_connections > 0) {
        server->active_connections--;
    }

    pthread_mutex_unlock(&g_lb->mutex);
}

void loadbalancer_mark_failed(backend_server_t *server) {
    if (!g_lb || !server) {
        return;
    }

    pthread_mutex_lock(&g_lb->mutex);

    server->failed_requests++;

    /* Mark unhealthy if too many failures */
    if (server->failed_requests > 5) {
        server->is_healthy = 0;
        printf("[WARN] Backend %s:%d marked unhealthy\n",
               server->host, server->port);
    }

    pthread_mutex_unlock(&g_lb->mutex);
}

/* Health check for backend servers */
int loadbalancer_health_check(backend_server_t *server) {
    /* Try to connect to backend */
    int fd = connect_to_host(server->host, server->port);
    if (fd < 0) {
        return -1;
    }

    close(fd);
    return 0;
}

void loadbalancer_run_health_checks(void) {
    if (!g_lb) {
        return;
    }

    pthread_mutex_lock(&g_lb->mutex);

    backend_server_t *server = g_lb->servers;
    time_t now = time(NULL);

    while (server) {
        /* Check every 30 seconds */
        if (difftime(now, server->last_health_check) >= 30) {
            if (loadbalancer_health_check(server) == 0) {
                if (!server->is_healthy) {
                    printf("[INFO] Backend %s:%d is now healthy\n",
                           server->host, server->port);
                }
                server->is_healthy = 1;
                server->failed_requests = 0;
            } else {
                if (server->is_healthy) {
                    printf("[WARN] Backend %s:%d health check failed\n",
                           server->host, server->port);
                }
                server->is_healthy = 0;
            }
            server->last_health_check = now;
        }
        server = server->next;
    }

    pthread_mutex_unlock(&g_lb->mutex);
}

void loadbalancer_print_stats(void) {
    if (!g_lb) {
        return;
    }

    pthread_mutex_lock(&g_lb->mutex);

    printf("\n[LOAD BALANCER] Backend Statistics:\n");
    backend_server_t *server = g_lb->servers;
    int index = 0;

    while (server) {
        printf("  [%d] %s:%d - Active: %d, Total: %d, Failed: %d, Status: %s\n",
               index++,
               server->host,
               server->port,
               server->active_connections,
               server->total_requests,
               server->failed_requests,
               server->is_healthy ? "Healthy" : "Unhealthy");
        server = server->next;
    }

    pthread_mutex_unlock(&g_lb->mutex);
}

void loadbalancer_cleanup(void) {
    if (!g_lb) {
        return;
    }

    pthread_mutex_lock(&g_lb->mutex);

    backend_server_t *server = g_lb->servers;
    while (server) {
        backend_server_t *next = server->next;
        free(server);
        server = next;
    }

    pthread_mutex_unlock(&g_lb->mutex);
    pthread_mutex_destroy(&g_lb->mutex);

    free(g_lb);
    g_lb = NULL;

    printf("[INFO] Load balancer cleaned up\n");
}
