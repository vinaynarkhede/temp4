#include "../../include/proxy.h"
#include <sys/time.h>

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int create_server_socket(const char *addr, int port) {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    /* Create socket */
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return -1;
    }

    /* Set socket options */
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }

    /* Bind */
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (strcmp(addr, "0.0.0.0") == 0) {
        address.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, addr, &address.sin_addr) <= 0) {
            fprintf(stderr, "Invalid address: %s\n", addr);
            close(server_fd);
            return -1;
        }
    }

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }

    /* Listen */
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

int connect_to_host(const char *host, int port) {
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    /* Create socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }

    /* Get host by name */
    server = gethostbyname(host);
    if (server == NULL) {
        close(sockfd);
        return -1;
    }

    /* Setup address structure */
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    /* Connect */
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

ssize_t recv_all(int fd, void *buffer, size_t length, int timeout) {
    size_t total = 0;
    ssize_t n;
    char *buf = (char *)buffer;

    /* Set timeout if specified */
    if (timeout > 0) {
        struct timeval tv;
        tv.tv_sec = timeout;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    while (total < length) {
        n = recv(fd, buf + total, length - total, 0);
        if (n <= 0) {
            if (n == 0) {
                /* Connection closed */
                break;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }
        total += n;
    }

    return total;
}

ssize_t send_all(int fd, const void *buffer, size_t length) {
    size_t total = 0;
    ssize_t n;
    const char *buf = (const char *)buffer;

    while (total < length) {
        n = send(fd, buf + total, length - total, MSG_NOSIGNAL);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }
        total += n;
    }

    return total;
}
