#include "../../include/proxy.h"

int http_parse_request(const char *buffer, size_t len, char *method,
                       char *url, char *version, char *host, int *port) {
    /* Parse first line: METHOD URL VERSION */
    const char *line_end = strstr(buffer, "\r\n");
    if (!line_end) {
        return -1;
    }

    /* Extract method, URL, version */
    if (sscanf(buffer, "%s %s %s", method, url, version) != 3) {
        return -1;
    }

    /* Parse Host header */
    const char *host_header = strcasestr(buffer, "Host:");
    if (host_header) {
        host_header += 5;
        while (*host_header == ' ') host_header++;

        const char *host_end = strstr(host_header, "\r\n");
        if (host_end) {
            int host_len = host_end - host_header;
            if (host_len > MAX_HOSTNAME - 1) {
                host_len = MAX_HOSTNAME - 1;
            }

            char host_port[MAX_HOSTNAME];
            strncpy(host_port, host_header, host_len);
            host_port[host_len] = '\0';

            /* Check for port */
            char *colon = strchr(host_port, ':');
            if (colon) {
                *colon = '\0';
                *port = atoi(colon + 1);
                strcpy(host, host_port);
            } else {
                strcpy(host, host_port);
                *port = 80;
            }
        }
    } else {
        /* Try to extract from URL */
        if (strncmp(url, "http://", 7) == 0) {
            char *host_start = url + 7;
            char *path_start = strchr(host_start, '/');

            if (path_start) {
                int host_len = path_start - host_start;
                if (host_len > MAX_HOSTNAME - 1) {
                    host_len = MAX_HOSTNAME - 1;
                }

                char host_port[MAX_HOSTNAME];
                strncpy(host_port, host_start, host_len);
                host_port[host_len] = '\0';

                char *colon = strchr(host_port, ':');
                if (colon) {
                    *colon = '\0';
                    *port = atoi(colon + 1);
                    strcpy(host, host_port);
                } else {
                    strcpy(host, host_port);
                    *port = 80;
                }
            }
        }
    }

    return 0;
}

int http_handle_request(connection_t *conn) {
    char buffer[BUFFER_SIZE];
    char method[16], url[MAX_PATH], version[16];
    char host[MAX_HOSTNAME];
    int port = 80;
    ssize_t n;

    /* Receive request */
    n = recv(conn->client_fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        return -1;
    }
    buffer[n] = '\0';

    /* Parse request */
    if (http_parse_request(buffer, n, method, url, version, host, &port) != 0) {
        const char *error_response =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n\r\n"
            "<html><body><h1>400 Bad Request</h1></body></html>\r\n";
        send_all(conn->client_fd, error_response, strlen(error_response));
        return -1;
    }

    if (g_config.enable_logging) {
        logger_log(1, "[%s] %s %s %s", conn->client_ip, method, url, host);
    }

    stats_increment_requests();

    /* Check authentication if enabled */
    if (g_config.enable_auth) {
        char username[128] = {0}, password[128] = {0};
        const char *auth_header = strcasestr(buffer, "Proxy-Authorization:");

        if (!auth_header || auth_parse_basic(auth_header, username, password) != 0 ||
            auth_verify(username, password) != 0) {
            const char *auth_required =
                "HTTP/1.1 407 Proxy Authentication Required\r\n"
                "Proxy-Authenticate: Basic realm=\"Proxy\"\r\n"
                "Connection: close\r\n\r\n";
            send_all(conn->client_fd, auth_required, strlen(auth_required));
            return -1;
        }
        conn->authenticated = 1;
    }

    /* Check URL filtering */
    if (g_config.enable_filtering && filter_check_url(url) != 0) {
        const char *forbidden =
            "HTTP/1.1 403 Forbidden\r\n"
            "Content-Type: text/html\r\n"
            "Connection: close\r\n\r\n"
            "<html><body><h1>403 Forbidden - Content Blocked</h1></body></html>\r\n";
        send_all(conn->client_fd, forbidden, strlen(forbidden));
        return -1;
    }

    /* Handle CONNECT method (for HTTPS tunneling) */
    if (strcmp(method, "CONNECT") == 0) {
        /* Parse host:port from URL */
        char *colon = strchr(url, ':');
        if (colon) {
            *colon = '\0';
            port = atoi(colon + 1);
            strcpy(host, url);
        }

        /* Connect to remote host */
        conn->remote_fd = connect_to_host(host, port);
        if (conn->remote_fd < 0) {
            const char *error_response =
                "HTTP/1.1 502 Bad Gateway\r\n"
                "Connection: close\r\n\r\n";
            send_all(conn->client_fd, error_response, strlen(error_response));
            return -1;
        }

        /* Send success response */
        const char *success_response = "HTTP/1.1 200 Connection Established\r\n\r\n";
        send_all(conn->client_fd, success_response, strlen(success_response));

        /* Tunnel data bidirectionally */
        fd_set readfds;
        while (1) {
            FD_ZERO(&readfds);
            FD_SET(conn->client_fd, &readfds);
            FD_SET(conn->remote_fd, &readfds);

            int max_fd = (conn->client_fd > conn->remote_fd) ?
                         conn->client_fd : conn->remote_fd;

            struct timeval tv = {.tv_sec = 300, .tv_usec = 0};
            int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

            if (activity <= 0) {
                break;
            }

            if (FD_ISSET(conn->client_fd, &readfds)) {
                n = recv(conn->client_fd, buffer, sizeof(buffer), 0);
                if (n <= 0) break;
                if (send_all(conn->remote_fd, buffer, n) < 0) break;
                conn->bytes_sent += n;
                stats_add_bytes_transferred(n);
            }

            if (FD_ISSET(conn->remote_fd, &readfds)) {
                n = recv(conn->remote_fd, buffer, sizeof(buffer), 0);
                if (n <= 0) break;
                if (send_all(conn->client_fd, buffer, n) < 0) break;
                conn->bytes_received += n;
                stats_add_bytes_transferred(n);
            }
        }

        return 0;
    }

    /* Check cache if enabled */
    if (g_config.enable_caching && strcmp(method, "GET") == 0) {
        void *cached_data = NULL;
        size_t cached_size = 0;

        if (cache_get(url, &cached_data, &cached_size) == 0) {
            if (g_config.enable_logging) {
                logger_log(1, "[CACHE HIT] %s", url);
            }
            send_all(conn->client_fd, cached_data, cached_size);
            free(cached_data);
            return 0;
        }
    }

    /* Forward request to remote server */
    conn->remote_fd = connect_to_host(host, port);
    if (conn->remote_fd < 0) {
        const char *error_response =
            "HTTP/1.1 502 Bad Gateway\r\n"
            "Connection: close\r\n\r\n";
        send_all(conn->client_fd, error_response, strlen(error_response));
        return -1;
    }

    /* Send request to remote server */
    if (send_all(conn->remote_fd, buffer, n) < 0) {
        return -1;
    }
    conn->bytes_sent += n;

    /* Receive response from remote server */
    char *response_buffer = NULL;
    size_t response_size = 0;
    size_t response_capacity = BUFFER_SIZE;

    if (g_config.enable_caching && strcmp(method, "GET") == 0) {
        response_buffer = malloc(response_capacity);
    }

    while (1) {
        n = recv(conn->remote_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            break;
        }

        /* Forward to client */
        if (send_all(conn->client_fd, buffer, n) < 0) {
            break;
        }

        conn->bytes_received += n;
        stats_add_bytes_transferred(n);

        /* Store in cache buffer if caching is enabled */
        if (response_buffer) {
            if (response_size + n > response_capacity) {
                response_capacity *= 2;
                char *new_buffer = realloc(response_buffer, response_capacity);
                if (new_buffer) {
                    response_buffer = new_buffer;
                } else {
                    free(response_buffer);
                    response_buffer = NULL;
                }
            }

            if (response_buffer) {
                memcpy(response_buffer + response_size, buffer, n);
                response_size += n;
            }
        }
    }

    /* Cache the response if enabled and successful */
    if (response_buffer && response_size > 0) {
        cache_put(url, response_buffer, response_size, g_config.cache_ttl_seconds);
        if (g_config.enable_logging) {
            logger_log(1, "[CACHE STORE] %s (%zu bytes)", url, response_size);
        }
    }

    if (response_buffer) {
        free(response_buffer);
    }

    return 0;
}
