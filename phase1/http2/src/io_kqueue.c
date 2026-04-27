#include "http2/io.h"

#if defined(HTTP2_BACKEND_KQUEUE)

#include <errno.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

const char *h2_io_backend_name(void)
{
    return "kqueue";
}

int h2_io_init(h2_io *io)
{
    if (io == NULL) {
        errno = EINVAL;
        return -1;
    }
    io->fd = kqueue();
    io->state = NULL;
    return io->fd >= 0 ? 0 : -1;
}

static int h2_io_kevent(int kq, int fd, int16_t filter, uint16_t flags)
{
    struct kevent change;

    EV_SET(&change, (uintptr_t)fd, filter, flags, 0u, 0, NULL);
    return kevent(kq, &change, 1, NULL, 0, NULL);
}

int h2_io_add_listener(h2_io *io, int fd)
{
    return h2_io_kevent(io->fd, fd, EVFILT_READ, EV_ADD | EV_ENABLE);
}

int h2_io_add_connection(h2_io *io, int fd)
{
    if (h2_io_kevent(io->fd, fd, EVFILT_READ, EV_ADD | EV_ENABLE) != 0) {
        return -1;
    }
    return h2_io_kevent(io->fd, fd, EVFILT_WRITE, EV_ADD | EV_DISABLE);
}

int h2_io_mod_connection(h2_io *io, int fd, bool want_write)
{
    return h2_io_kevent(io->fd, fd, EVFILT_WRITE, want_write ? EV_ENABLE : EV_DISABLE);
}

int h2_io_remove(h2_io *io, int fd)
{
    (void)h2_io_kevent(io->fd, fd, EVFILT_READ, EV_DELETE);
    (void)h2_io_kevent(io->fd, fd, EVFILT_WRITE, EV_DELETE);
    return 0;
}

int h2_io_wait(h2_io *io, h2_event *events, size_t event_cap, int timeout_ms)
{
    struct kevent kernel_events[64];
    struct timespec timeout;
    struct timespec *timeout_ptr;
    int count;
    int pos;

    if (event_cap > sizeof(kernel_events) / sizeof(kernel_events[0])) {
        event_cap = sizeof(kernel_events) / sizeof(kernel_events[0]);
    }
    timeout_ptr = NULL;
    if (timeout_ms >= 0) {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
        timeout_ptr = &timeout;
    }
    count = kevent(io->fd, NULL, 0, kernel_events, (int)event_cap, timeout_ptr);
    if (count < 0) {
        return errno == EINTR ? 0 : -1;
    }
    for (pos = 0; pos < count; pos++) {
        events[pos].fd = (int)kernel_events[pos].ident;
        events[pos].readable = kernel_events[pos].filter == EVFILT_READ;
        events[pos].writable = kernel_events[pos].filter == EVFILT_WRITE;
        events[pos].closed = (kernel_events[pos].flags & (EV_EOF | EV_ERROR)) != 0;
    }
    return count;
}

void h2_io_close(h2_io *io)
{
    if (io != NULL && io->fd >= 0) {
        close(io->fd);
        io->fd = -1;
    }
}
#endif
