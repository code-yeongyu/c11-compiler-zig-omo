#ifndef HTTP2_IO_H
#define HTTP2_IO_H

#include <stdbool.h>
#include <stddef.h>

typedef struct h2_io {
    int fd;
    void *state;
} h2_io;

typedef struct h2_event {
    int fd;
    bool readable;
    bool writable;
    bool closed;
} h2_event;

const char *h2_io_backend_name(void);
int h2_io_init(h2_io *io);
int h2_io_add_listener(h2_io *io, int fd);
int h2_io_add_connection(h2_io *io, int fd);
int h2_io_mod_connection(h2_io *io, int fd, bool want_write);
int h2_io_remove(h2_io *io, int fd);
int h2_io_wait(h2_io *io, h2_event *events, size_t event_cap, int timeout_ms);
void h2_io_close(h2_io *io);

#endif
