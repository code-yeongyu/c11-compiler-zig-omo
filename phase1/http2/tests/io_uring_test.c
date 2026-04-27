#define H2_URING_FORCE_TIMEOUT_SQE 1
#include "../src/io_uring.c"

#include <stdio.h>

#define EXPECT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)
#define EXPECT_EQ_INT(got, want) EXPECT_TRUE((int)(got) == (int)(want))
#define EXPECT_EQ_U32(got, want) EXPECT_TRUE((uint32_t)(got) == (uint32_t)(want))

static void init_fake_ring(h2_uring_state *state, unsigned *sq_head, unsigned *sq_tail, unsigned *sq_mask, unsigned *sq_entries, unsigned *sq_array, struct io_uring_sqe *sqes, unsigned *cq_head, unsigned *cq_tail, unsigned *cq_mask, struct io_uring_cqe *cqes)
{
    memset(state, 0, sizeof(*state));
    state->ring_fd = -1;
    state->sq_head = sq_head;
    state->sq_tail = sq_tail;
    state->sq_ring_mask = sq_mask;
    state->sq_ring_entries = sq_entries;
    state->sq_array = sq_array;
    state->sqes = sqes;
    state->cq_head = cq_head;
    state->cq_tail = cq_tail;
    state->cq_ring_mask = cq_mask;
    state->cqes = cqes;
}

static int test_poll_remove_preserves_unrelated_cqes(void)
{
    h2_uring_state state;
    unsigned char cqe_storage[2u * sizeof(struct io_uring_cqe)];
    struct io_uring_cqe *cqes;
    struct io_uring_sqe sqes[2];
    unsigned sq_array[2];
    unsigned sq_head = 0u;
    unsigned sq_tail = 0u;
    unsigned sq_mask = 1u;
    unsigned sq_entries = 2u;
    unsigned cq_head = 0u;
    unsigned cq_tail = 2u;
    unsigned cq_mask = 1u;
    h2_event event;
    int out_count;

    /* given a cancel completion queued behind an unrelated readiness completion */
    cqes = (struct io_uring_cqe *)cqe_storage;
    init_fake_ring(&state, &sq_head, &sq_tail, &sq_mask, &sq_entries, sq_array, sqes, &cq_head, &cq_tail, &cq_mask, cqes);
    state.fds[11].used = true;
    state.fds[11].active = true;
    cqes[0].user_data = h2_uring_poll_user_data(11);
    cqes[0].res = POLLIN;
    cqes[1].user_data = h2_uring_cancel_user_data(5);
    cqes[1].res = -ENOENT;

    /* when waiting for the POLL_REMOVE completion */
    EXPECT_EQ_INT(h2_uring_drain_until_cancel(&state, 5), 0);

    /* then the unrelated completion is saved for the normal event path */
    EXPECT_EQ_U32(state.pending_cqe_count, 1u);
    EXPECT_TRUE(state.fds[11].active);
    out_count = 0;
    h2_uring_finish_saved_cqes(&state, &event, 1u, &out_count);
    EXPECT_EQ_INT(out_count, 1);
    EXPECT_EQ_INT(event.fd, 11);
    EXPECT_TRUE(event.readable);
    return 0;
}

static int test_finite_timeout_fallback_submits_timeout_sqe(void)
{
    h2_uring_state state;
    unsigned char cqe_storage[sizeof(struct io_uring_cqe)];
    struct io_uring_cqe *cqes;
    struct io_uring_sqe sqes[2];
    unsigned sq_array[2];
    unsigned sq_head = 0u;
    unsigned sq_tail = 0u;
    unsigned sq_mask = 1u;
    unsigned sq_entries = 2u;
    unsigned cq_head = 0u;
    unsigned cq_tail = 0u;
    unsigned cq_mask = 0u;

    /* given EXT_ARG timeout waits are unavailable to this build */
    cqes = (struct io_uring_cqe *)cqe_storage;
    init_fake_ring(&state, &sq_head, &sq_tail, &sq_mask, &sq_entries, sq_array, sqes, &cq_head, &cq_tail, &cq_mask, cqes);

    /* when a finite wait timeout is requested */
    EXPECT_EQ_INT(h2_uring_wait_for_event(&state, 25) < 0, 1);

    /* then an IORING_OP_TIMEOUT SQE is submitted instead of a blocking wait */
    EXPECT_EQ_U32(sq_tail, 1u);
    EXPECT_EQ_INT(sqes[0].opcode, IORING_OP_TIMEOUT);
    EXPECT_EQ_U32((uint32_t)sqes[0].len, 1u);
    EXPECT_TRUE(h2_uring_is_timeout_user_data(sqes[0].user_data));
    return 0;
}

static int test_h2_io_remove_handles_full_ring_of_deferred_cqes(void)
{
    h2_uring_state state;
    unsigned char cqe_storage[H2_URING_ENTRIES * sizeof(struct io_uring_cqe)];
    struct io_uring_cqe *cqes;
    struct io_uring_sqe sqes[1];
    unsigned sq_array[1];
    unsigned sq_head = 0u;
    unsigned sq_tail = 0u;
    unsigned sq_mask = 0u;
    unsigned sq_entries = 1u;
    unsigned cq_head = 0u;
    unsigned cq_tail = H2_URING_ENTRIES;
    unsigned cq_mask = H2_URING_ENTRIES - 1u;
    unsigned pos;

    /* given a full CQ ring with the cancel completion behind unrelated completions {[http2-engineer]} */
    cqes = (struct io_uring_cqe *)cqe_storage;
    init_fake_ring(&state, &sq_head, &sq_tail, &sq_mask, &sq_entries, sq_array, sqes, &cq_head, &cq_tail, &cq_mask, cqes);
    for (pos = 0u; pos + 1u < H2_URING_ENTRIES; pos++) {
        cqes[pos].user_data = h2_uring_poll_user_data((int)(100u + pos));
        cqes[pos].res = POLLIN;
    }
    cqes[H2_URING_ENTRIES - 1u].user_data = h2_uring_cancel_user_data(5);
    cqes[H2_URING_ENTRIES - 1u].res = -ENOENT;

    /* when h2_io_remove drains to the POLL_REMOVE completion {[http2-engineer]} */
    EXPECT_EQ_INT(h2_uring_drain_until_cancel(&state, 5), 0);

    /* then every unrelated CQE fits in the deferred buffer without EAGAIN {[http2-engineer]} */
    EXPECT_EQ_U32(state.pending_cqe_count, H2_URING_ENTRIES - 1u);
    EXPECT_EQ_U32(cq_head, H2_URING_ENTRIES);
    return 0;
}

int main(void)
{
    EXPECT_EQ_INT(test_poll_remove_preserves_unrelated_cqes(), 0);
    EXPECT_EQ_INT(test_finite_timeout_fallback_submits_timeout_sqe(), 0);
    EXPECT_EQ_INT(test_h2_io_remove_handles_full_ring_of_deferred_cqes(), 0);
    puts("io_uring_test: ok");
    return 0;
}
