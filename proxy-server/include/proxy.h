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

/* Load Balancing Algorithms */
typedef enum {
    LB_ALGORITHM_ROUND_ROBIN,
    LB_ALGORITHM_LEAST_CONNECTIONS,
    LB_ALGORITHM_IP_HASH,
    LB_ALGORITHM_WEIGHTED_RR
} lb_algorithm_t;

/* Compression Types */
typedef enum {
    COMPRESS_NONE = 0,
    COMPRESS_GZIP = 1,
    COMPRESS_DEFLATE = 2
} compression_type_t;

/* Forward declarations */
typedef struct proxy_config proxy_config_t;
typedef struct thread_pool thread_pool_t;
typedef struct cache_entry cache_entry_t;
typedef struct auth_credentials auth_credentials_t;
typedef struct backend_server backend_server_t;
typedef struct ssl_context ssl_context_t;

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
    char **backend_hosts;
    int *backend_ports;
    int *backend_weights;
    lb_algorithm_t lb_algorithm;

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

/* monitor/ratelimit.c */
void ratelimit_init(void);
int ratelimit_check_request(const char *ip);
int ratelimit_check_bandwidth(const char *ip, size_t bytes, int direction);
void ratelimit_update_bandwidth(const char *ip, size_t bytes, int direction);
void ratelimit_cleanup_old_entries(void);
void ratelimit_print_stats(void);
void ratelimit_cleanup(void);

/* core/loadbalancer.c */
int loadbalancer_init(void);
backend_server_t* loadbalancer_select_backend(const char *client_ip);
void loadbalancer_release_backend(backend_server_t *server);
void loadbalancer_mark_failed(backend_server_t *server);
void loadbalancer_run_health_checks(void);
void loadbalancer_print_stats(void);
void loadbalancer_cleanup(void);

/* protocols/compression.c */
void compression_init(void);
int compression_check_accept_encoding(const char *headers);
int compression_should_compress(const char *content_type, size_t content_length);
int compression_gzip_compress(const unsigned char *input, size_t input_len,
                               unsigned char **output, size_t *output_len);
int compression_deflate_compress(const unsigned char *input, size_t input_len,
                                  unsigned char **output, size_t *output_len);
int compression_gzip_decompress(const unsigned char *input, size_t input_len,
                                 unsigned char **output, size_t *output_len);
int compression_add_encoding_header(char *response, size_t response_size,
                                     const char *encoding);
int compression_update_content_length(char *response, size_t new_length);
void compression_cleanup(void);

/* protocols/ssl.c */
int ssl_init(void);
int ssl_create_server_context(const char *cert_file, const char *key_file);
ssl_context_t* ssl_create_client_context(int sockfd);
ssl_context_t* ssl_accept_connection(int sockfd);
ssize_t ssl_read(ssl_context_t *ssl_ctx, void *buf, size_t len);
ssize_t ssl_write(ssl_context_t *ssl_ctx, const void *buf, size_t len);
int ssl_get_peer_cert_info(ssl_context_t *ssl_ctx, char *buf, size_t buf_size);
const char* ssl_get_cipher_info(ssl_context_t *ssl_ctx);
void ssl_shutdown(ssl_context_t *ssl_ctx);
int ssl_generate_self_signed_cert(const char *cert_file, const char *key_file);
void ssl_cleanup(void);

#endif /* PROXY_H */
