#ifndef PROXY_H
#define PROXY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <netdb.h>

/* Configuration Constants */
#define MAX_EVENTS 1024
#define BUFFER_SIZE 65536
#define MAX_THREADS 100
#define MAX_CONNECTIONS 10000
#define MAX_HOSTNAME 256
#define MAX_PATH 4096
#define BACKLOG 128

/* Proxy Modes */
typedef enum {
    PROXY_MODE_HTTP,
    PROXY_MODE_HTTPS,
    PROXY_MODE_SOCKS5,
    PROXY_MODE_TCP,
    PROXY_MODE_REVERSE,
    PROXY_MODE_TRANSPARENT
} proxy_mode_t;

/* Threading Models */
typedef enum {
    THREAD_MODEL_PER_CONNECTION,
    THREAD_MODEL_POOL,
    THREAD_MODEL_EVENT_DRIVEN,
    THREAD_MODEL_HYBRID
} thread_model_t;

/* Forward declarations */
typedef struct proxy_config proxy_config_t;
typedef struct thread_pool thread_pool_t;
typedef struct cache_entry cache_entry_t;
typedef struct auth_credentials auth_credentials_t;

/* Connection structure */
typedef struct connection {
    int client_fd;
    int remote_fd;
    struct sockaddr_in client_addr;
    time_t created_at;
    size_t bytes_sent;
    size_t bytes_received;
    char client_ip[INET_ADDRSTRLEN];
    int authenticated;
    void *ssl_ctx;  /* SSL context if SSL is enabled */
} connection_t;

/* Main proxy configuration */
struct proxy_config {
    int port;
    char bind_address[64];
    proxy_mode_t mode;
    thread_model_t thread_model;
    int num_threads;
    int max_connections;

    /* Feature flags */
    int enable_caching;
    int enable_ssl;
    int enable_auth;
    int enable_logging;
    int enable_filtering;
    int enable_compression;
    int enable_rate_limiting;

    /* SSL/TLS settings */
    char ssl_cert_file[MAX_PATH];
    char ssl_key_file[MAX_PATH];

    /* Cache settings */
    size_t cache_size_mb;
    int cache_ttl_seconds;

    /* Auth settings */
    char auth_file[MAX_PATH];

    /* Reverse proxy settings */
    char backend_host[MAX_HOSTNAME];
    int backend_port;
    int num_backends;

    /* Rate limiting */
    int rate_limit_requests;
    int rate_limit_bandwidth_kbps;

    /* ACL */
    char **whitelist_ips;
    char **blacklist_ips;
    int whitelist_count;
    int blacklist_count;

    /* Logging */
    char log_file[MAX_PATH];
    int log_level;
};

/* Global proxy instance */
extern proxy_config_t g_config;
extern volatile int g_running;

/* Function declarations - will be implemented in separate modules */

/* core/server.c */
int proxy_server_init(proxy_config_t *config);
int proxy_server_start(void);
void proxy_server_stop(void);
void proxy_server_cleanup(void);

/* core/connection.c */
connection_t* connection_create(int client_fd);
void connection_destroy(connection_t *conn);
int connection_handle(connection_t *conn);

/* core/threading.c */
thread_pool_t* thread_pool_create(int num_threads);
void thread_pool_destroy(thread_pool_t *pool);
int thread_pool_submit(thread_pool_t *pool, void (*func)(void*), void *arg);

/* protocols/http.c */
int http_handle_request(connection_t *conn);
int http_parse_request(const char *buffer, size_t len, char *method,
                        char *url, char *version, char *host, int *port);

/* protocols/socks5.c */
int socks5_handle_handshake(connection_t *conn);
int socks5_handle_request(connection_t *conn);

/* cache/cache.c */
int cache_init(size_t size_mb);
int cache_get(const char *key, void **data, size_t *size);
int cache_put(const char *key, const void *data, size_t size, int ttl);
void cache_cleanup(void);

/* auth/auth.c */
int auth_init(const char *auth_file);
int auth_verify(const char *username, const char *password);
int auth_parse_basic(const char *auth_header, char *username, char *password);
void auth_cleanup(void);

/* filter/filter.c */
int filter_init(void);
int filter_check_url(const char *url);
int filter_check_ip(const char *ip);
void filter_cleanup(void);

/* monitor/logger.c */
void logger_init(const char *log_file, int log_level);
void logger_log(int level, const char *fmt, ...);
void logger_cleanup(void);

/* monitor/stats.c */
void stats_init(void);
void stats_increment_connections(void);
void stats_increment_requests(void);
void stats_add_bytes_transferred(size_t bytes);
void stats_print(void);

/* utils/network.c */
int set_nonblocking(int fd);
int create_server_socket(const char *addr, int port);
int connect_to_host(const char *host, int port);
ssize_t recv_all(int fd, void *buffer, size_t length, int timeout);
ssize_t send_all(int fd, const void *buffer, size_t length);

/* utils/config.c */
int config_load(const char *config_file, proxy_config_t *config);
void config_set_defaults(proxy_config_t *config);

#endif /* PROXY_H */
