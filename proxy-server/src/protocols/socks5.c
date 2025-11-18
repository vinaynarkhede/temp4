#include "../../include/proxy.h"

/* SOCKS5 protocol constants */
#define SOCKS5_VERSION 0x05
#define SOCKS5_AUTH_NONE 0x00
#define SOCKS5_AUTH_USERPASS 0x02
#define SOCKS5_AUTH_NO_ACCEPTABLE 0xFF

#define SOCKS5_CMD_CONNECT 0x01
#define SOCKS5_CMD_BIND 0x02
#define SOCKS5_CMD_UDP 0x03

#define SOCKS5_ATYP_IPV4 0x01
#define SOCKS5_ATYP_DOMAIN 0x03
#define SOCKS5_ATYP_IPV6 0x04

#define SOCKS5_REP_SUCCESS 0x00
#define SOCKS5_REP_FAILURE 0x01
#define SOCKS5_REP_NOT_ALLOWED 0x02
#define SOCKS5_REP_NETWORK_UNREACHABLE 0x03
#define SOCKS5_REP_HOST_UNREACHABLE 0x04
#define SOCKS5_REP_CONNECTION_REFUSED 0x05
#define SOCKS5_REP_TTL_EXPIRED 0x06
#define SOCKS5_REP_CMD_NOT_SUPPORTED 0x07
#define SOCKS5_REP_ATYP_NOT_SUPPORTED 0x08

int socks5_handle_handshake(connection_t *conn) {
    unsigned char buffer[256];
    ssize_t n;

    /* Receive client greeting */
    n = recv(conn->client_fd, buffer, sizeof(buffer), 0);
    if (n < 2) {
        return -1;
    }

    /* Check version */
    if (buffer[0] != SOCKS5_VERSION) {
        return -1;
    }

    /* Get number of auth methods */
    int num_methods = buffer[1];
    if (n < 2 + num_methods) {
        return -1;
    }

    /* Check supported auth methods */
    unsigned char selected_method = SOCKS5_AUTH_NO_ACCEPTABLE;

    for (int i = 0; i < num_methods; i++) {
        unsigned char method = buffer[2 + i];

        if (g_config.enable_auth && method == SOCKS5_AUTH_USERPASS) {
            selected_method = SOCKS5_AUTH_USERPASS;
            break;
        } else if (!g_config.enable_auth && method == SOCKS5_AUTH_NONE) {
            selected_method = SOCKS5_AUTH_NONE;
            break;
        }
    }

    /* Send method selection response */
    unsigned char response[2] = {SOCKS5_VERSION, selected_method};
    if (send_all(conn->client_fd, response, 2) < 0) {
        return -1;
    }

    if (selected_method == SOCKS5_AUTH_NO_ACCEPTABLE) {
        return -1;
    }

    /* Handle username/password authentication if required */
    if (selected_method == SOCKS5_AUTH_USERPASS) {
        /* Receive auth request */
        n = recv(conn->client_fd, buffer, sizeof(buffer), 0);
        if (n < 3) {
            return -1;
        }

        /* Version should be 0x01 for username/password auth */
        if (buffer[0] != 0x01) {
            return -1;
        }

        /* Extract username */
        int username_len = buffer[1];
        if (n < 2 + username_len + 1) {
            return -1;
        }

        char username[256] = {0};
        memcpy(username, buffer + 2, username_len);

        /* Extract password */
        int password_len = buffer[2 + username_len];
        if (n < 2 + username_len + 1 + password_len) {
            return -1;
        }

        char password[256] = {0};
        memcpy(password, buffer + 2 + username_len + 1, password_len);

        /* Verify credentials */
        int auth_result = auth_verify(username, password);

        /* Send auth response */
        unsigned char auth_response[2] = {0x01, (auth_result == 0) ? 0x00 : 0x01};
        if (send_all(conn->client_fd, auth_response, 2) < 0) {
            return -1;
        }

        if (auth_result != 0) {
            return -1;
        }

        conn->authenticated = 1;
    }

    return 0;
}

int socks5_handle_request(connection_t *conn) {
    unsigned char buffer[512];
    ssize_t n;

    /* Receive request */
    n = recv(conn->client_fd, buffer, sizeof(buffer), 0);
    if (n < 4) {
        return -1;
    }

    /* Parse request */
    unsigned char version = buffer[0];
    unsigned char cmd = buffer[1];
    unsigned char atyp = buffer[3];

    if (version != SOCKS5_VERSION) {
        return -1;
    }

    char host[MAX_HOSTNAME] = {0};
    int port = 0;
    int addr_offset = 4;

    /* Parse address */
    if (atyp == SOCKS5_ATYP_IPV4) {
        if (n < 10) {
            return -1;
        }
        sprintf(host, "%d.%d.%d.%d",
                buffer[4], buffer[5], buffer[6], buffer[7]);
        port = (buffer[8] << 8) | buffer[9];
    } else if (atyp == SOCKS5_ATYP_DOMAIN) {
        int domain_len = buffer[4];
        if (n < 5 + domain_len + 2) {
            return -1;
        }
        memcpy(host, buffer + 5, domain_len);
        host[domain_len] = '\0';
        port = (buffer[5 + domain_len] << 8) | buffer[5 + domain_len + 1];
    } else if (atyp == SOCKS5_ATYP_IPV6) {
        /* IPv6 not fully implemented in this example */
        unsigned char reply[10] = {
            SOCKS5_VERSION, SOCKS5_REP_ATYP_NOT_SUPPORTED,
            0x00, SOCKS5_ATYP_IPV4,
            0, 0, 0, 0, 0, 0
        };
        send_all(conn->client_fd, reply, sizeof(reply));
        return -1;
    } else {
        unsigned char reply[10] = {
            SOCKS5_VERSION, SOCKS5_REP_ATYP_NOT_SUPPORTED,
            0x00, SOCKS5_ATYP_IPV4,
            0, 0, 0, 0, 0, 0
        };
        send_all(conn->client_fd, reply, sizeof(reply));
        return -1;
    }

    if (g_config.enable_logging) {
        logger_log(1, "[SOCKS5] %s CONNECT %s:%d", conn->client_ip, host, port);
    }

    /* Handle command */
    if (cmd == SOCKS5_CMD_CONNECT) {
        /* Connect to remote host */
        conn->remote_fd = connect_to_host(host, port);

        unsigned char reply[10];
        if (conn->remote_fd < 0) {
            /* Connection failed */
            reply[0] = SOCKS5_VERSION;
            reply[1] = SOCKS5_REP_HOST_UNREACHABLE;
            reply[2] = 0x00;
            reply[3] = SOCKS5_ATYP_IPV4;
            memset(reply + 4, 0, 6);
            send_all(conn->client_fd, reply, 10);
            return -1;
        }

        /* Connection successful */
        reply[0] = SOCKS5_VERSION;
        reply[1] = SOCKS5_REP_SUCCESS;
        reply[2] = 0x00;
        reply[3] = SOCKS5_ATYP_IPV4;

        /* Get local address */
        struct sockaddr_in local_addr;
        socklen_t addr_len = sizeof(local_addr);
        getsockname(conn->remote_fd, (struct sockaddr *)&local_addr, &addr_len);

        memcpy(reply + 4, &local_addr.sin_addr, 4);
        memcpy(reply + 8, &local_addr.sin_port, 2);

        if (send_all(conn->client_fd, reply, 10) < 0) {
            return -1;
        }

        /* Relay data bidirectionally */
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

    } else if (cmd == SOCKS5_CMD_BIND || cmd == SOCKS5_CMD_UDP) {
        /* Not implemented */
        unsigned char reply[10] = {
            SOCKS5_VERSION, SOCKS5_REP_CMD_NOT_SUPPORTED,
            0x00, SOCKS5_ATYP_IPV4,
            0, 0, 0, 0, 0, 0
        };
        send_all(conn->client_fd, reply, sizeof(reply));
        return -1;
    }

    return -1;
}
