#include "http2/io.h"

#if defined(HTTP2_BACKEND_IO_URING)

#include <errno.h>
#include <linux/io_uring.h>
#include <linux/time_types.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#define H2_URING_ENTRIES 256u
#define H2_URING_MAX_FDS 4096u
#define H2_URING_CANCEL_FLAG UINT64_C(0x8000000000000000)
#define H2_URING_TIMEOUT_FLAG UINT64_C(0x4000000000000000)
#define H2_URING_FD_MASK UINT64_C(0x00000000ffffffff)
#define H2_URING_SAVED_CQES H2_URING_ENTRIES

typedef struct h2_uring_fd_state {
    bool used;
    bool active;
    uint32_t events;
    uint32_t generation;
} h2_uring_fd_state;

typedef struct h2_uring_saved_cqe {
    uint64_t user_data;
    int32_t res;
    uint32_t flags;
} h2_uring_saved_cqe;

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
    struct __kernel_timespec timeout;
    h2_uring_saved_cqe pending_cqes[H2_URING_SAVED_CQES];
    unsigned pending_cqe_count;
    h2_uring_fd_state fds[H2_URING_MAX_FDS];
} h2_uring_state;

static int h2_uring_cancel_active_poll(h2_uring_state *state, int fd);

const char *h2_io_backend_name(void)
{
    return "io_uring";
}

static int h2_uring_enter(int fd, unsigned to_submit, unsigned min_complete, unsigned flags, const void *arg, size_t arg_size)
{
    return (int)syscall(__NR_io_uring_enter, fd, to_submit, min_complete, flags, arg, arg_size);
}

static uint64_t h2_uring_poll_user_data(int fd, uint32_t generation)
{
    return ((uint64_t)generation << 32) | (uint64_t)(uint32_t)fd;
}

static uint64_t h2_uring_cancel_user_data(int fd, uint32_t generation)
{
    return H2_URING_CANCEL_FLAG | h2_uring_poll_user_data(fd, generation);
}

static bool h2_uring_is_cancel_user_data(uint64_t user_data)
{
    return (user_data & H2_URING_CANCEL_FLAG) != 0u;
}

static bool h2_uring_is_timeout_user_data(uint64_t user_data)
{
    return (user_data & H2_URING_TIMEOUT_FLAG) != 0u;
}

static int h2_uring_user_data_fd(uint64_t user_data)
{
    return (int)(user_data & H2_URING_FD_MASK);
}

static uint32_t h2_uring_user_data_generation(uint64_t user_data)
{
    return (uint32_t)((user_data & UINT64_C(0x3fffffff00000000)) >> 32);
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
    if (used && !state->fds[fd].used) {
        state->fds[fd].generation++;
    }
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
    h2_uring_state *state;

    if (io == NULL || io->state == NULL || fd < 0 || fd >= (int)H2_URING_MAX_FDS) {
        errno = EINVAL;
        return -1;
    }
    state = io->state;
    if (!state->fds[fd].used) {
        return 0;
    }
    if (h2_uring_cancel_active_poll(state, fd) != 0) {
        return -1;
    }
    state->fds[fd].active = false;
    state->fds[fd].used = false;
    state->fds[fd].events = 0u;
    return 0;
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

static void h2_uring_finish_cqe(h2_uring_state *state, const struct io_uring_cqe *cqe, h2_event *events, size_t event_cap, int *out_count)
{
    uint64_t user_data;
    int fd;
    int result;
    uint32_t generation;

    user_data = cqe->user_data;
    if (h2_uring_is_timeout_user_data(user_data)) {
        return;
    }
    fd = h2_uring_user_data_fd(user_data);
    result = cqe->res;
    if (fd < 0 || fd >= (int)H2_URING_MAX_FDS) {
        return;
    }
    generation = h2_uring_user_data_generation(user_data);
    if (h2_uring_is_cancel_user_data(user_data)) {
        if (generation == state->fds[fd].generation) {
            state->fds[fd].active = false;
        }
        return;
    }
    if (generation != state->fds[fd].generation) {
        return;
    }
    state->fds[fd].active = false;
    if (state->fds[fd].used && events != NULL && (size_t)*out_count < event_cap) {
        events[*out_count].fd = fd;
        events[*out_count].readable = result > 0 && ((uint32_t)result & POLLIN) != 0u;
        events[*out_count].writable = result > 0 && ((uint32_t)result & POLLOUT) != 0u;
        events[*out_count].closed = result < 0 || (result > 0 && ((uint32_t)result & (POLLERR | POLLHUP | POLLRDHUP)) != 0u);
        (*out_count)++;
    }
}

static int h2_uring_save_cqe(h2_uring_state *state, const struct io_uring_cqe *cqe)
{
    if (state->pending_cqe_count >= H2_URING_SAVED_CQES) {
        errno = EAGAIN;
        return -1;
    }
    state->pending_cqes[state->pending_cqe_count].user_data = cqe->user_data;
    state->pending_cqes[state->pending_cqe_count].res = cqe->res;
    state->pending_cqes[state->pending_cqe_count].flags = cqe->flags;
    state->pending_cqe_count++;
    return 0;
}

static void h2_uring_finish_saved_cqes(h2_uring_state *state, h2_event *events, size_t event_cap, int *out_count)
{
    unsigned pos;
    unsigned keep;

    pos = 0u;
    keep = 0u;
    while (pos < state->pending_cqe_count) {
        if ((size_t)*out_count < event_cap) {
            struct io_uring_cqe cqe;

            memset(&cqe, 0, sizeof(cqe));
            cqe.user_data = state->pending_cqes[pos].user_data;
            cqe.res = state->pending_cqes[pos].res;
            cqe.flags = state->pending_cqes[pos].flags;
            h2_uring_finish_cqe(state, &cqe, events, event_cap, out_count);
        } else {
            state->pending_cqes[keep] = state->pending_cqes[pos];
            keep++;
        }
        pos++;
    }
    state->pending_cqe_count = keep;
}

static int h2_uring_drain_until_cancel(h2_uring_state *state, int fd)
{
    for (;;) {
        unsigned head;
        unsigned tail;

        if (*state->cq_head == *state->cq_tail) {
            if (h2_uring_enter(state->ring_fd, 0u, 1u, IORING_ENTER_GETEVENTS, NULL, 0u) < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return -1;
            }
        }
        head = *state->cq_head;
        tail = *state->cq_tail;
        while (head != tail) {
            struct io_uring_cqe *cqe;
            uint64_t user_data;

            cqe = &state->cqes[head & *state->cq_ring_mask];
            user_data = cqe->user_data;
            if (user_data == h2_uring_cancel_user_data(fd, state->fds[fd].generation)) {
                head++;
                *state->cq_head = head;
                return cqe->res == -ENOENT || cqe->res >= 0 ? 0 : -1;
            }
            if (h2_uring_save_cqe(state, cqe) != 0) {
                return -1;
            }
            head++;
        }
        *state->cq_head = head;
    }
}

static int h2_uring_cancel_active_poll(h2_uring_state *state, int fd)
{
    struct io_uring_sqe *sqe;

    if (!state->fds[fd].active) {
        return 0;
    }
    sqe = h2_uring_get_sqe(state);
    if (sqe == NULL) {
        errno = EAGAIN;
        return -1;
    }
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_POLL_REMOVE;
    sqe->addr = h2_uring_poll_user_data(fd, state->fds[fd].generation);
    sqe->user_data = h2_uring_cancel_user_data(fd, state->fds[fd].generation);
    if (state->ring_fd >= 0 && h2_uring_enter(state->ring_fd, 1u, 0u, 0u, NULL, 0u) < 0) {
        return -1;
    }
    return h2_uring_drain_until_cancel(state, fd);
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
        sqe->user_data = h2_uring_poll_user_data(fd, state->fds[fd].generation);
        state->fds[fd].active = true;
        submitted++;
    }
    if (submitted == 0u) {
        return 0;
    }
    return h2_uring_enter(state->ring_fd, submitted, 0u, 0u, NULL, 0u) < 0 ? -1 : 0;
}

#if !defined(IORING_ENTER_EXT_ARG) || defined(H2_URING_FORCE_TIMEOUT_SQE)
static int h2_uring_submit_timeout_sqe(h2_uring_state *state, int timeout_ms)
{
    struct io_uring_sqe *sqe;
    sqe = h2_uring_get_sqe(state);
    if (sqe == NULL) {
        errno = EAGAIN;
        return -1;
    }
    memset(sqe, 0, sizeof(*sqe));
    state->timeout.tv_sec = timeout_ms / 1000;
    state->timeout.tv_nsec = (long long)(timeout_ms % 1000) * 1000000ll;
    sqe->opcode = IORING_OP_TIMEOUT;
    sqe->addr = (uint64_t)(uintptr_t)&state->timeout;
    sqe->len = 1u;
    sqe->user_data = H2_URING_TIMEOUT_FLAG;
    return h2_uring_enter(state->ring_fd, 1u, 1u, IORING_ENTER_GETEVENTS, NULL, 0u);
}
#endif

static int h2_uring_wait_for_event(h2_uring_state *state, int timeout_ms)
{
    if (timeout_ms == 0) {
        return 0;
    }
    if (timeout_ms < 0) {
        return h2_uring_enter(state->ring_fd, 0u, 1u, IORING_ENTER_GETEVENTS, NULL, 0u);
    }
#if defined(IORING_ENTER_EXT_ARG) && !defined(H2_URING_FORCE_TIMEOUT_SQE)
    {
        struct __kernel_timespec timeout;
        struct io_uring_getevents_arg arg;

        memset(&arg, 0, sizeof(arg));
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_nsec = (long long)(timeout_ms % 1000) * 1000000ll;
        arg.ts = (uint64_t)(uintptr_t)&timeout;
        return h2_uring_enter(state->ring_fd, 0u, 1u, IORING_ENTER_GETEVENTS | IORING_ENTER_EXT_ARG, &arg, sizeof(arg));
    }
#else
    return h2_uring_submit_timeout_sqe(state, timeout_ms);
#endif
}

int h2_io_wait(h2_io *io, h2_event *events, size_t event_cap, int timeout_ms)
{
    h2_uring_state *state;
    unsigned head;
    unsigned tail;
    int out_count;

    state = io->state;
    if (event_cap == 0u) {
        return 0;
    }
    if (h2_uring_submit_missing(state) != 0) {
        return -1;
    }
    if (*state->cq_head == *state->cq_tail) {
        if (h2_uring_wait_for_event(state, timeout_ms) < 0) {
            if (errno == EINTR) {
                return 0;
            }
#ifdef ETIME
            if (errno == ETIME) {
                return 0;
            }
#endif
            return errno == ETIMEDOUT ? 0 : -1;
        }
    }
    out_count = 0;
    h2_uring_finish_saved_cqes(state, events, event_cap, &out_count);
    head = *state->cq_head;
    tail = *state->cq_tail;
    while (head != tail && (size_t)out_count < event_cap) {
        struct io_uring_cqe *cqe;

        cqe = &state->cqes[head & *state->cq_ring_mask];
        h2_uring_finish_cqe(state, cqe, events, event_cap, &out_count);
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
