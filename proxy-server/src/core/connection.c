#include "../../include/proxy.h"

connection_t* connection_create(int client_fd) {
    connection_t *conn = malloc(sizeof(connection_t));
    if (!conn) {
        return NULL;
    }

    memset(conn, 0, sizeof(connection_t));
    conn->client_fd = client_fd;
    conn->remote_fd = -1;
    conn->created_at = time(NULL);
    conn->authenticated = 0;
    conn->ssl_ctx = NULL;

    /* Get client address */
    socklen_t addr_len = sizeof(conn->client_addr);
    getpeername(client_fd, (struct sockaddr *)&conn->client_addr, &addr_len);
    inet_ntop(AF_INET, &conn->client_addr.sin_addr,
              conn->client_ip, INET_ADDRSTRLEN);

    stats_increment_connections();

    return conn;
}

void connection_destroy(connection_t *conn) {
    if (!conn) {
        return;
    }

    if (conn->client_fd >= 0) {
        close(conn->client_fd);
    }

    if (conn->remote_fd >= 0) {
        close(conn->remote_fd);
    }

    free(conn);
}

/* Relay data between client and remote server */
static int relay_data(int from_fd, int to_fd, size_t *bytes_transferred) {
    char buffer[BUFFER_SIZE];
    ssize_t n;

    n = recv(from_fd, buffer, sizeof(buffer), 0);
    if (n <= 0) {
        return -1;
    }

    if (send_all(to_fd, buffer, n) < 0) {
        return -1;
    }

    *bytes_transferred += n;
    stats_add_bytes_transferred(n);

    return 0;
}

/* Handle connection based on proxy mode */
int connection_handle(connection_t *conn) {
    if (!conn) {
        return -1;
    }

    /* Check IP filtering */
    if (g_config.enable_filtering) {
        if (filter_check_ip(conn->client_ip) != 0) {
            fprintf(stderr, "[FILTER] Blocked IP: %s\n", conn->client_ip);
            return -1;
        }
    }

    /* Handle based on proxy mode */
    int result = -1;

    switch (g_config.mode) {
        case PROXY_MODE_HTTP:
        case PROXY_MODE_HTTPS:
            result = http_handle_request(conn);
            break;

        case PROXY_MODE_SOCKS5:
            if (socks5_handle_handshake(conn) == 0) {
                result = socks5_handle_request(conn);
            }
            break;

        case PROXY_MODE_TCP:
        case PROXY_MODE_REVERSE:
            /* Simple TCP relay to backend */
            conn->remote_fd = connect_to_host(g_config.backend_host,
                                              g_config.backend_port);
            if (conn->remote_fd < 0) {
                return -1;
            }

            /* Bidirectional relay */
            fd_set readfds;
            while (1) {
                FD_ZERO(&readfds);
                FD_SET(conn->client_fd, &readfds);
                FD_SET(conn->remote_fd, &readfds);

                int max_fd = (conn->client_fd > conn->remote_fd) ?
                             conn->client_fd : conn->remote_fd;

                struct timeval tv = {.tv_sec = 30, .tv_usec = 0};
                int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

                if (activity <= 0) {
                    break;
                }

                if (FD_ISSET(conn->client_fd, &readfds)) {
                    if (relay_data(conn->client_fd, conn->remote_fd,
                                   &conn->bytes_sent) < 0) {
                        break;
                    }
                }

                if (FD_ISSET(conn->remote_fd, &readfds)) {
                    if (relay_data(conn->remote_fd, conn->client_fd,
                                   &conn->bytes_received) < 0) {
                        break;
                    }
                }
            }
            result = 0;
            break;

        default:
            fprintf(stderr, "[ERROR] Unsupported proxy mode\n");
            break;
    }

    return result;
}
