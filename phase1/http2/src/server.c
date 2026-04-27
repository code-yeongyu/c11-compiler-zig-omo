#include "http2/server.h"

#include "http2/connection.h"
#include "http2/io.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define H2_SERVER_MAX_CLIENTS 128u

typedef struct h2_client {
    int fd;
    bool used;
    h2_connection conn;
} h2_client;

static int h2_set_nonblock(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int h2_create_listener(const h2_server_config *config, uint16_t *bound_port)
{
    struct sockaddr_in addr;
    socklen_t addr_len;
    int fd;
    int yes;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    yes = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_NOSIGPIPE
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
    if (h2_set_nonblock(fd) != 0) {
        close(fd);
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config->port);
    if (inet_pton(AF_INET, config->host, &addr.sin_addr) != 1) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 128) != 0) {
        close(fd);
        return -1;
    }
    addr_len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &addr_len) != 0) {
        close(fd);
        return -1;
    }
    *bound_port = ntohs(addr.sin_port);
    return fd;
}

static int h2_write_ready_file(const char *path, uint16_t port)
{
    FILE *file;

    if (path == NULL) {
        return 0;
    }
    file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    fprintf(file, "%u\n", (unsigned)port);
    if (fclose(file) != 0) {
        return -1;
    }
    return 0;
}

static void h2_log_stack_limit(void)
{
    struct rlimit limit;

    if (getrlimit(RLIMIT_STACK, &limit) != 0) {
        return;
    }
    if (limit.rlim_cur == RLIM_INFINITY) {
        fprintf(stderr, "h2d stack limit: unlimited\n");
    } else {
        fprintf(stderr, "h2d stack limit: %llu bytes\n", (unsigned long long)limit.rlim_cur);
    }
}

static h2_client *h2_find_client(h2_client *clients, int fd)
{
    size_t pos;

    for (pos = 0u; pos < H2_SERVER_MAX_CLIENTS; pos++) {
        if (clients[pos].used && clients[pos].fd == fd) {
            return &clients[pos];
        }
    }
    return NULL;
}

static h2_client *h2_alloc_client(h2_client *clients, int fd)
{
    size_t pos;

    for (pos = 0u; pos < H2_SERVER_MAX_CLIENTS; pos++) {
        if (!clients[pos].used) {
            clients[pos].fd = fd;
            clients[pos].used = true;
            h2_connection_init(&clients[pos].conn);
            return &clients[pos];
        }
    }
    return NULL;
}

static unsigned h2_active_client_count(const h2_client *clients)
{
    unsigned count;
    size_t pos;

    count = 0u;
    for (pos = 0u; pos < H2_SERVER_MAX_CLIENTS; pos++) {
        if (clients[pos].used) {
            count++;
        }
    }
    return count;
}

static void h2_close_client(h2_io *io, h2_client *client)
{
    if (client == NULL || !client->used) {
        return;
    }
    (void)h2_io_remove(io, client->fd);
    close(client->fd);
    memset(client, 0, sizeof(*client));
    client->fd = -1;
}

static int h2_flush_client(h2_client *client)
{
    while (h2_connection_wants_write(&client->conn)) {
        ssize_t written;

        written = write(client->fd, h2_connection_output(&client->conn), h2_connection_output_len(&client->conn));
        if (written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return 0;
            }
            return -1;
        }
        if (written == 0) {
            return 0;
        }
        h2_connection_consume_output(&client->conn, (size_t)written);
    }
    return 0;
}

static int h2_read_client(h2_client *client)
{
    uint8_t buffer[8192];

    for (;;) {
        ssize_t got;

        got = read(client->fd, buffer, sizeof(buffer));
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (got == 0) {
            return 1;
        }
        if (h2_connection_feed(&client->conn, buffer, (size_t)got) != H2_OK && !h2_connection_wants_write(&client->conn)) {
            return -1;
        }
    }
}

static int h2_accept_clients(h2_io *io, h2_client *clients, int listener_fd, unsigned *accepted_count)
{
    for (;;) {
        struct sockaddr_in addr;
        socklen_t addr_len;
        h2_client *client;
        int fd;

        addr_len = sizeof(addr);
        fd = accept(listener_fd, (struct sockaddr *)&addr, &addr_len);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (h2_set_nonblock(fd) != 0) {
            close(fd);
            continue;
        }
        client = h2_alloc_client(clients, fd);
        if (client == NULL) {
            close(fd);
            continue;
        }
        if (h2_io_add_connection(io, fd) != 0) {
            h2_close_client(io, client);
            continue;
        }
        (*accepted_count)++;
    }
}

int h2_server_run(const h2_server_config *config)
{
    h2_client *clients;
    h2_event events[64];
    h2_io io;
    uint16_t bound_port;
    unsigned accepted_count;
    int listener_fd;
    int exit_code;

    if (config == NULL || config->host == NULL) {
        errno = EINVAL;
        return -1;
    }
    signal(SIGPIPE, SIG_IGN);
    h2_log_stack_limit();
    /* h2_connection owns large I/O buffers; keep the client table off the startup stack. */
    clients = calloc(H2_SERVER_MAX_CLIENTS, sizeof(*clients));
    if (clients == NULL) {
        return -1;
    }
    listener_fd = h2_create_listener(config, &bound_port);
    if (listener_fd < 0) {
        free(clients);
        return -1;
    }
    if (h2_io_init(&io) != 0) {
        close(listener_fd);
        free(clients);
        return -1;
    }
    if (h2_io_add_listener(&io, listener_fd) != 0 || h2_write_ready_file(config->ready_file, bound_port) != 0) {
        h2_io_close(&io);
        close(listener_fd);
        free(clients);
        return -1;
    }
    fprintf(stderr, "h2d listening on %s:%u using %s\n", config->host, (unsigned)bound_port, h2_io_backend_name());
    accepted_count = 0u;
    exit_code = 0;
    for (;;) {
        int count;
        int pos;

        if (config->max_connections > 0u && accepted_count >= config->max_connections && h2_active_client_count(clients) == 0u) {
            break;
        }
        count = h2_io_wait(&io, events, sizeof(events) / sizeof(events[0]), -1);
        if (count < 0) {
            exit_code = -1;
            break;
        }
        for (pos = 0; pos < count; pos++) {
            h2_client *client;
            bool close_after;

            if (events[pos].fd == listener_fd) {
                if (events[pos].readable && h2_accept_clients(&io, clients, listener_fd, &accepted_count) != 0) {
                    exit_code = -1;
                }
                continue;
            }
            client = h2_find_client(clients, events[pos].fd);
            if (client == NULL) {
                continue;
            }
            close_after = false;
            if (events[pos].readable) {
                int read_result;

                read_result = h2_read_client(client);
                if (read_result < 0) {
                    close_after = true;
                } else if (read_result > 0) {
                    close_after = true;
                }
            }
            if (events[pos].closed) {
                close_after = true;
            }
            if (h2_connection_wants_write(&client->conn) && h2_flush_client(client) != 0) {
                close_after = true;
            }
            if (close_after && !h2_connection_wants_write(&client->conn)) {
                h2_close_client(&io, client);
            } else if (!close_after) {
                (void)h2_io_mod_connection(&io, client->fd, h2_connection_wants_write(&client->conn));
            }
        }
        if (exit_code != 0) {
            break;
        }
    }
    for (size_t pos = 0u; pos < H2_SERVER_MAX_CLIENTS; pos++) {
        if (clients[pos].used) {
            h2_close_client(&io, &clients[pos]);
        }
    }
    h2_io_close(&io);
    close(listener_fd);
    free(clients);
    return exit_code;
}
