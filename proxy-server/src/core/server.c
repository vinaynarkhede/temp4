#include "../../include/proxy.h"

static int g_server_fd = -1;
static thread_pool_t *g_thread_pool = NULL;
static int g_epoll_fd = -1;

/* Worker function for thread pool */
static void handle_connection_worker(void *arg) {
    connection_t *conn = (connection_t *)arg;

    if (connection_handle(conn) < 0) {
        if (g_config.enable_logging) {
            logger_log(2, "Connection from %s failed", conn->client_ip);
        }
    } else {
        if (g_config.enable_logging) {
            logger_log(1, "Connection from %s completed: sent=%zu, recv=%zu",
                      conn->client_ip, conn->bytes_sent, conn->bytes_received);
        }
    }

    connection_destroy(conn);
}

/* Thread-per-connection handler */
static void* handle_connection_thread(void *arg) {
    connection_t *conn = (connection_t *)arg;
    handle_connection_worker(conn);
    return NULL;
}

int proxy_server_init(proxy_config_t *config) {
    /* Create server socket */
    g_server_fd = create_server_socket(config->bind_address, config->port);
    if (g_server_fd < 0) {
        return -1;
    }

    /* Initialize threading based on model */
    if (config->thread_model == THREAD_MODEL_POOL ||
        config->thread_model == THREAD_MODEL_HYBRID) {
        g_thread_pool = thread_pool_create(config->num_threads);
        if (!g_thread_pool) {
            close(g_server_fd);
            return -1;
        }
    }

    /* Initialize epoll for event-driven or hybrid mode */
    if (config->thread_model == THREAD_MODEL_EVENT_DRIVEN ||
        config->thread_model == THREAD_MODEL_HYBRID) {
        g_epoll_fd = epoll_create1(0);
        if (g_epoll_fd < 0) {
            perror("epoll_create1");
            if (g_thread_pool) {
                thread_pool_destroy(g_thread_pool);
            }
            close(g_server_fd);
            return -1;
        }

        /* Add server socket to epoll */
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = g_server_fd;
        if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, g_server_fd, &ev) < 0) {
            perror("epoll_ctl");
            close(g_epoll_fd);
            if (g_thread_pool) {
                thread_pool_destroy(g_thread_pool);
            }
            close(g_server_fd);
            return -1;
        }

        /* Set server socket to non-blocking */
        set_nonblocking(g_server_fd);
    }

    return 0;
}

/* Event-driven server loop using epoll */
static int server_loop_event_driven(void) {
    struct epoll_event events[MAX_EVENTS];

    while (g_running) {
        int nfds = epoll_wait(g_epoll_fd, events, MAX_EVENTS, 1000);

        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == g_server_fd) {
                /* Accept new connection */
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);

                int client_fd = accept(g_server_fd,
                                      (struct sockaddr *)&client_addr,
                                      &addr_len);
                if (client_fd < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        perror("accept");
                    }
                    continue;
                }

                connection_t *conn = connection_create(client_fd);
                if (!conn) {
                    close(client_fd);
                    continue;
                }

                /* For hybrid mode, submit to thread pool */
                if (g_config.thread_model == THREAD_MODEL_HYBRID) {
                    thread_pool_submit(g_thread_pool, handle_connection_worker, conn);
                } else {
                    /* Handle in event loop (simplified - real implementation
                       would use epoll for the connection too) */
                    handle_connection_worker(conn);
                }
            }
        }
    }

    return 0;
}

/* Thread pool server loop */
static int server_loop_thread_pool(void) {
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(g_server_fd, (struct sockaddr *)&client_addr,
                              &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        connection_t *conn = connection_create(client_fd);
        if (!conn) {
            close(client_fd);
            continue;
        }

        /* Submit to thread pool */
        if (thread_pool_submit(g_thread_pool, handle_connection_worker, conn) < 0) {
            fprintf(stderr, "[ERROR] Failed to submit work to thread pool\n");
            connection_destroy(conn);
        }
    }

    return 0;
}

/* Thread-per-connection server loop */
static int server_loop_per_connection(void) {
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(g_server_fd, (struct sockaddr *)&client_addr,
                              &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        connection_t *conn = connection_create(client_fd);
        if (!conn) {
            close(client_fd);
            continue;
        }

        /* Create new thread for each connection */
        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        if (pthread_create(&thread, &attr, handle_connection_thread, conn) != 0) {
            fprintf(stderr, "[ERROR] Failed to create thread\n");
            connection_destroy(conn);
        }

        pthread_attr_destroy(&attr);
    }

    return 0;
}

int proxy_server_start(void) {
    if (g_server_fd < 0) {
        return -1;
    }

    printf("[INFO] Server started successfully\n");
    printf("[INFO] Press Ctrl+C to stop\n\n");

    /* Start appropriate server loop based on threading model */
    int result = 0;

    switch (g_config.thread_model) {
        case THREAD_MODEL_EVENT_DRIVEN:
        case THREAD_MODEL_HYBRID:
            result = server_loop_event_driven();
            break;

        case THREAD_MODEL_POOL:
            result = server_loop_thread_pool();
            break;

        case THREAD_MODEL_PER_CONNECTION:
            result = server_loop_per_connection();
            break;

        default:
            fprintf(stderr, "[ERROR] Unknown threading model\n");
            result = -1;
            break;
    }

    return result;
}

void proxy_server_stop(void) {
    g_running = 0;
}

void proxy_server_cleanup(void) {
    if (g_thread_pool) {
        thread_pool_destroy(g_thread_pool);
        g_thread_pool = NULL;
    }

    if (g_epoll_fd >= 0) {
        close(g_epoll_fd);
        g_epoll_fd = -1;
    }

    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }

    stats_print();
}
