#include "http2/io.h"

#if defined(HTTP2_BACKEND_IO_URING)

#include <errno.h>
#include <linux/io_uring.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#define H2_URING_ENTRIES 256u
#define H2_URING_MAX_FDS 4096u

typedef struct h2_uring_fd_state {
    bool used;
    bool active;
    uint32_t events;
} h2_uring_fd_state;

typedef struct h2_uring_state {
    int ring_fd;
    void *sq_ring_ptr;
    void *cq_ring_ptr;
    void *sqes_ptr;
    size_t sq_ring_size;
    size_t cq_ring_size;
    size_t sqes_size;
    unsigned *sq_head;
    unsigned *sq_tail;
    unsigned *sq_ring_mask;
    unsigned *sq_ring_entries;
    unsigned *sq_array;
    struct io_uring_sqe *sqes;
    unsigned *cq_head;
    unsigned *cq_tail;
    unsigned *cq_ring_mask;
    struct io_uring_cqe *cqes;
    h2_uring_fd_state fds[H2_URING_MAX_FDS];
} h2_uring_state;

const char *h2_io_backend_name(void)
{
    return "io_uring";
}

static int h2_uring_enter(int fd, unsigned to_submit, unsigned min_complete, unsigned flags)
{
    return (int)syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags, NULL, 0);
}

static void *h2_uring_offset(void *base, unsigned offset)
{
    return (uint8_t *)base + offset;
}

static int h2_uring_map(h2_uring_state *state, const struct io_uring_params *params)
{
    state->sq_ring_size = params->sq_off.array + params->sq_entries * sizeof(unsigned);
    state->cq_ring_size = params->cq_off.cqes + params->cq_entries * sizeof(struct io_uring_cqe);
    if ((params->features & IORING_FEAT_SINGLE_MMAP) != 0u) {
        size_t ring_size;

        ring_size = state->sq_ring_size > state->cq_ring_size ? state->sq_ring_size : state->cq_ring_size;
        state->sq_ring_size = ring_size;
        state->cq_ring_size = ring_size;
        state->sq_ring_ptr = mmap(NULL, ring_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, state->ring_fd, IORING_OFF_SQ_RING);
        if (state->sq_ring_ptr == MAP_FAILED) {
            return -1;
        }
        state->cq_ring_ptr = state->sq_ring_ptr;
    } else {
        state->sq_ring_ptr = mmap(NULL, state->sq_ring_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, state->ring_fd, IORING_OFF_SQ_RING);
        if (state->sq_ring_ptr == MAP_FAILED) {
            return -1;
        }
        state->cq_ring_ptr = mmap(NULL, state->cq_ring_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, state->ring_fd, IORING_OFF_CQ_RING);
        if (state->cq_ring_ptr == MAP_FAILED) {
            return -1;
        }
    }
    state->sqes_size = params->sq_entries * sizeof(struct io_uring_sqe);
    state->sqes_ptr = mmap(NULL, state->sqes_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, state->ring_fd, IORING_OFF_SQES);
    if (state->sqes_ptr == MAP_FAILED) {
        return -1;
    }
    state->sq_head = h2_uring_offset(state->sq_ring_ptr, params->sq_off.head);
    state->sq_tail = h2_uring_offset(state->sq_ring_ptr, params->sq_off.tail);
    state->sq_ring_mask = h2_uring_offset(state->sq_ring_ptr, params->sq_off.ring_mask);
    state->sq_ring_entries = h2_uring_offset(state->sq_ring_ptr, params->sq_off.ring_entries);
    state->sq_array = h2_uring_offset(state->sq_ring_ptr, params->sq_off.array);
    state->sqes = state->sqes_ptr;
    state->cq_head = h2_uring_offset(state->cq_ring_ptr, params->cq_off.head);
    state->cq_tail = h2_uring_offset(state->cq_ring_ptr, params->cq_off.tail);
    state->cq_ring_mask = h2_uring_offset(state->cq_ring_ptr, params->cq_off.ring_mask);
    state->cqes = h2_uring_offset(state->cq_ring_ptr, params->cq_off.cqes);
    return 0;
}

int h2_io_init(h2_io *io)
{
    struct io_uring_params params;
    h2_uring_state *state;

    if (io == NULL) {
        errno = EINVAL;
        return -1;
    }
    state = calloc(1u, sizeof(*state));
    if (state == NULL) {
        return -1;
    }
    memset(&params, 0, sizeof(params));
    state->ring_fd = (int)syscall(__NR_io_uring_setup, H2_URING_ENTRIES, &params);
    if (state->ring_fd < 0) {
        free(state);
        return -1;
    }
    if (h2_uring_map(state, &params) != 0) {
        h2_io tmp;

        tmp.fd = -1;
        tmp.state = state;
        h2_io_close(&tmp);
        return -1;
    }
    io->fd = state->ring_fd;
    io->state = state;
    return 0;
}

static int h2_uring_set_fd(h2_io *io, int fd, bool used, uint32_t events)
{
    h2_uring_state *state;

    if (fd < 0 || fd >= (int)H2_URING_MAX_FDS) {
        errno = EMFILE;
        return -1;
    }
    state = io->state;
    state->fds[fd].used = used;
    state->fds[fd].events = events;
    if (!used) {
        state->fds[fd].active = false;
    }
    return 0;
}

int h2_io_add_listener(h2_io *io, int fd)
{
    return h2_uring_set_fd(io, fd, true, POLLIN | POLLERR | POLLHUP);
}

int h2_io_add_connection(h2_io *io, int fd)
{
    return h2_uring_set_fd(io, fd, true, POLLIN | POLLRDHUP | POLLERR | POLLHUP);
}

int h2_io_mod_connection(h2_io *io, int fd, bool want_write)
{
    uint32_t events;

    events = POLLIN | POLLRDHUP | POLLERR | POLLHUP;
    if (want_write) {
        events |= POLLOUT;
    }
    return h2_uring_set_fd(io, fd, true, events);
}

int h2_io_remove(h2_io *io, int fd)
{
    return h2_uring_set_fd(io, fd, false, 0u);
}

static struct io_uring_sqe *h2_uring_get_sqe(h2_uring_state *state)
{
    unsigned head;
    unsigned tail;
    unsigned index;

    head = *state->sq_head;
    tail = *state->sq_tail;
    if (tail - head >= *state->sq_ring_entries) {
        return NULL;
    }
    index = tail & *state->sq_ring_mask;
    state->sq_array[index] = index;
    *state->sq_tail = tail + 1u;
    return &state->sqes[index];
}

static int h2_uring_submit_missing(h2_uring_state *state)
{
    unsigned submitted;
    int fd;

    submitted = 0u;
    for (fd = 0; fd < (int)H2_URING_MAX_FDS; fd++) {
        struct io_uring_sqe *sqe;

        if (!state->fds[fd].used || state->fds[fd].active) {
            continue;
        }
        sqe = h2_uring_get_sqe(state);
        if (sqe == NULL) {
            break;
        }
        memset(sqe, 0, sizeof(*sqe));
        sqe->opcode = IORING_OP_POLL_ADD;
        sqe->fd = fd;
        sqe->poll_events = (uint16_t)state->fds[fd].events;
        sqe->user_data = (uint64_t)(uint32_t)fd;
        state->fds[fd].active = true;
        submitted++;
    }
    if (submitted == 0u) {
        return 0;
    }
    return h2_uring_enter(state->ring_fd, submitted, 0u, 0u) < 0 ? -1 : 0;
}

int h2_io_wait(h2_io *io, h2_event *events, size_t event_cap, int timeout_ms)
{
    h2_uring_state *state;
    unsigned head;
    unsigned tail;
    int out_count;

    (void)timeout_ms;
    state = io->state;
    if (h2_uring_submit_missing(state) != 0) {
        return -1;
    }
    if (*state->cq_head == *state->cq_tail) {
        if (h2_uring_enter(state->ring_fd, 0u, 1u, IORING_ENTER_GETEVENTS) < 0) {
            return errno == EINTR ? 0 : -1;
        }
    }
    out_count = 0;
    head = *state->cq_head;
    tail = *state->cq_tail;
    while (head != tail && (size_t)out_count < event_cap) {
        struct io_uring_cqe *cqe;
        int fd;
        int result;

        cqe = &state->cqes[head & *state->cq_ring_mask];
        fd = (int)cqe->user_data;
        result = cqe->res;
        if (fd >= 0 && fd < (int)H2_URING_MAX_FDS) {
            state->fds[fd].active = false;
            if (state->fds[fd].used) {
                events[out_count].fd = fd;
                events[out_count].readable = result > 0 && ((uint32_t)result & POLLIN) != 0u;
                events[out_count].writable = result > 0 && ((uint32_t)result & POLLOUT) != 0u;
                events[out_count].closed = result < 0 || (result > 0 && ((uint32_t)result & (POLLERR | POLLHUP | POLLRDHUP)) != 0u);
                out_count++;
            }
        }
        head++;
    }
    *state->cq_head = head;
    return out_count;
}

void h2_io_close(h2_io *io)
{
    h2_uring_state *state;

    if (io == NULL || io->state == NULL) {
        return;
    }
    state = io->state;
    if (state->sqes_ptr != NULL && state->sqes_ptr != MAP_FAILED) {
        munmap(state->sqes_ptr, state->sqes_size);
    }
    if (state->sq_ring_ptr != NULL && state->sq_ring_ptr != MAP_FAILED) {
        munmap(state->sq_ring_ptr, state->sq_ring_size);
    }
    if (state->cq_ring_ptr != NULL && state->cq_ring_ptr != MAP_FAILED && state->cq_ring_ptr != state->sq_ring_ptr) {
        munmap(state->cq_ring_ptr, state->cq_ring_size);
    }
    if (state->ring_fd >= 0) {
        close(state->ring_fd);
    }
    free(state);
    io->fd = -1;
    io->state = NULL;
}
#endif
