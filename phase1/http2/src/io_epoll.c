#include "http2/io.h"

#if defined(HTTP2_BACKEND_EPOLL)

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

const char *h2_io_backend_name(void)
{
    return "epoll";
}

int h2_io_init(h2_io *io)
{
    if (io == NULL) {
        errno = EINVAL;
        return -1;
    }
    io->fd = epoll_create1(EPOLL_CLOEXEC);
    io->state = NULL;
    return io->fd >= 0 ? 0 : -1;
}

static int h2_io_ctl(h2_io *io, int op, int fd, uint32_t events)
{
    struct epoll_event event;

    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.fd = fd;
    return epoll_ctl(io->fd, op, fd, &event);
}

int h2_io_add_listener(h2_io *io, int fd)
{
    return h2_io_ctl(io, EPOLL_CTL_ADD, fd, EPOLLIN | EPOLLRDHUP);
}

int h2_io_add_connection(h2_io *io, int fd)
{
    return h2_io_ctl(io, EPOLL_CTL_ADD, fd, EPOLLIN | EPOLLRDHUP);
}

int h2_io_mod_connection(h2_io *io, int fd, bool want_write)
{
    uint32_t events;

    events = EPOLLIN | EPOLLRDHUP;
    if (want_write) {
        events |= EPOLLOUT;
    }
    return h2_io_ctl(io, EPOLL_CTL_MOD, fd, events);
}

int h2_io_remove(h2_io *io, int fd)
{
    (void)epoll_ctl(io->fd, EPOLL_CTL_DEL, fd, NULL);
    return 0;
}

int h2_io_wait(h2_io *io, h2_event *events, size_t event_cap, int timeout_ms)
{
    struct epoll_event kernel_events[64];
    int count;
    int pos;

    if (event_cap > sizeof(kernel_events) / sizeof(kernel_events[0])) {
        event_cap = sizeof(kernel_events) / sizeof(kernel_events[0]);
    }
    count = epoll_wait(io->fd, kernel_events, (int)event_cap, timeout_ms);
    if (count < 0) {
        return errno == EINTR ? 0 : -1;
    }
    for (pos = 0; pos < count; pos++) {
        events[pos].fd = kernel_events[pos].data.fd;
        events[pos].readable = (kernel_events[pos].events & EPOLLIN) != 0u;
        events[pos].writable = (kernel_events[pos].events & EPOLLOUT) != 0u;
        events[pos].closed = (kernel_events[pos].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0u;
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
