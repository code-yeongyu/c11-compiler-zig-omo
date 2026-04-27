#include "http2/stream.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#define EXPECT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)
#define EXPECT_EQ_INT(got, want) EXPECT_TRUE((int)(got) == (int)(want))
#define EXPECT_EQ_U32(got, want) EXPECT_TRUE((uint32_t)(got) == (uint32_t)(want))

static int test_send_window_add_avoids_signed_overflow(void)
{
    h2_stream stream;

    /* given a stream with a negative send window from prior flow-control pressure */
    h2_stream_init(&stream, 1u, -1);

    /* when a WINDOW_UPDATE increment is valid but would overflow the old subtraction check */
    /* then the update is accepted without evaluating signed overflow */
    EXPECT_EQ_INT(h2_stream_add_send_window(&stream, (uint32_t)INT32_MAX), H2_OK);
    EXPECT_EQ_INT(stream.send_window, INT32_MAX - 1);
    return 0;
}

static int test_closed_stream_reclaims_slot_id(void)
{
    h2_stream stream;

    /* given a stream that is half-closed remote after a complete request */
    h2_stream_init(&stream, 3u, H2_DEFAULT_WINDOW_SIZE);
    EXPECT_EQ_INT(h2_stream_receive_headers(&stream, true), H2_OK);

    /* when the server sends END_STREAM */
    EXPECT_EQ_INT(h2_stream_send_end_stream(&stream), H2_OK);

    /* then the slot id is cleared for reuse */
    EXPECT_EQ_U32(stream.id, 0u);
    EXPECT_EQ_INT(stream.state, H2_STREAM_STATE_CLOSED);
    return 0;
}

int main(void)
{
    EXPECT_EQ_INT(test_send_window_add_avoids_signed_overflow(), 0);
    EXPECT_EQ_INT(test_closed_stream_reclaims_slot_id(), 0);
    puts("stream_test: ok");
    return 0;
}
