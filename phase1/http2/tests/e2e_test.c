#include "http2/connection.h"
#include "http2/hpack.h"

#include <stdio.h>
#include <string.h>

#define EXPECT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)
#define EXPECT_EQ_INT(got, want) EXPECT_TRUE((int)(got) == (int)(want))
#define EXPECT_EQ_U32(got, want) EXPECT_TRUE((uint32_t)(got) == (uint32_t)(want))

static size_t append_preface_settings(uint8_t *wire, size_t cap, int zero_stream_window)
{
    h2_setting settings[2];
    size_t settings_len;
    size_t wire_pos;
    size_t frame_len;

    if (cap < H2_CLIENT_PREFACE_LEN) {
        return 0u;
    }
    wire_pos = 0u;
    memcpy(wire + wire_pos, H2_CLIENT_PREFACE, H2_CLIENT_PREFACE_LEN);
    wire_pos += H2_CLIENT_PREFACE_LEN;
    settings[0].id = H2_SETTINGS_ENABLE_PUSH;
    settings[0].value = 0u;
    settings_len = 1u;
    if (zero_stream_window) {
        settings[1].id = H2_SETTINGS_INITIAL_WINDOW_SIZE;
        settings[1].value = 0u;
        settings_len = 2u;
    }
    frame_len = h2_frame_encode_settings(wire + wire_pos, cap - wire_pos, 0u, settings, settings_len);
    return frame_len == 0u ? 0u : wire_pos + frame_len;
}

static size_t append_get_root(uint8_t *wire, size_t cap, uint32_t stream_id)
{
    uint8_t header_block[32];
    size_t pos;
    size_t chunk_len;

    pos = 0u;
    chunk_len = h2_hpack_encode_indexed(header_block + pos, sizeof(header_block) - pos, 2u);
    if (chunk_len == 0u) {
        return 0u;
    }
    pos += chunk_len;
    chunk_len = h2_hpack_encode_indexed(header_block + pos, sizeof(header_block) - pos, 6u);
    if (chunk_len == 0u) {
        return 0u;
    }
    pos += chunk_len;
    chunk_len = h2_hpack_encode_indexed(header_block + pos, sizeof(header_block) - pos, 4u);
    if (chunk_len == 0u) {
        return 0u;
    }
    pos += chunk_len;
    return h2_frame_encode_headers(wire, cap, stream_id, H2_FLAG_END_HEADERS | H2_FLAG_END_STREAM, header_block, pos);
}

static int output_has_type(const h2_connection *conn, uint8_t frame_type)
{
    const uint8_t *out;
    size_t out_len;
    size_t pos;

    out = h2_connection_output(conn);
    out_len = h2_connection_output_len(conn);
    pos = 0u;
    while (out != NULL && pos + H2_FRAME_HEADER_LEN <= out_len) {
        h2_frame_header header;

        if (h2_frame_parse_header(out + pos, out_len - pos, &header) != H2_OK) {
            return -1;
        }
        pos += H2_FRAME_HEADER_LEN;
        if (pos + header.length > out_len) {
            return -1;
        }
        if (header.type == frame_type) {
            return 1;
        }
        pos += header.length;
    }
    return 0;
}

static int test_e2e_200_stream_rollover(void)
{
    h2_connection conn;
    uint8_t wire[256];
    size_t wire_len;
    uint32_t stream_id;
    int index;

    /* given an established h2c connection */
    h2_connection_init(&conn);
    wire_len = append_preface_settings(wire, sizeof(wire), 0);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));

    /* when 200 sequential streams are opened, responded to, and closed */
    stream_id = 1u;
    for (index = 0; index < 200; index++) {
        wire_len = append_get_root(wire, sizeof(wire), stream_id);
        EXPECT_TRUE(wire_len > 0u);
        EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);

        /* then closed slots are reclaimed and DATA still appears after stream 128 */
        EXPECT_EQ_INT(output_has_type(&conn, H2_FRAME_DATA), 1);
        h2_connection_consume_output(&conn, h2_connection_output_len(&conn));
        stream_id += 2u;
    }
    EXPECT_EQ_U32(conn.responses_sent, 200u);
    return 0;
}

static int test_e2e_malformed_hpack_integer_rejected(void)
{
    h2_connection conn;
    uint8_t wire[256];
    uint8_t bad_block[] = { 0xffu, 0x80u, 0x80u, 0x80u, 0x80u, 0x80u, 0x00u };
    size_t wire_len;

    /* given an established connection */
    h2_connection_init(&conn);
    wire_len = append_preface_settings(wire, sizeof(wire), 0);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));

    /* when a HEADERS block contains an overlong HPACK integer */
    wire_len = h2_frame_encode_headers(wire, sizeof(wire), 1u, H2_FLAG_END_HEADERS | H2_FLAG_END_STREAM, bad_block, sizeof(bad_block));
    EXPECT_TRUE(wire_len > 0u);

    /* then it is rejected as HPACK compression corruption without UB */
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_COMPRESSION_ERROR);
    return 0;
}

static int test_e2e_blocked_window_defers_data(void)
{
    h2_connection conn;
    uint8_t wire[256];
    size_t wire_len;

    /* given a stream opened with zero initial send-window budget */
    h2_connection_init(&conn);
    wire_len = append_preface_settings(wire, sizeof(wire), 1);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));

    /* when response DATA is initially blocked */
    wire_len = append_get_root(wire, sizeof(wire), 1u);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);

    /* then DATA is deferred until WINDOW_UPDATE arrives */
    EXPECT_EQ_INT(output_has_type(&conn, H2_FRAME_HEADERS), 1);
    EXPECT_EQ_INT(output_has_type(&conn, H2_FRAME_DATA), 0);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));
    wire_len = h2_frame_encode_window_update(wire, sizeof(wire), 1u, 2u);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    EXPECT_EQ_INT(output_has_type(&conn, H2_FRAME_DATA), 1);
    return 0;
}

static int test_e2e_priority_self_dependency_rejected(void)
{
    h2_connection conn;
    h2_priority_payload priority;
    uint8_t wire[64];
    size_t wire_len;

    /* given an established connection */
    h2_connection_init(&conn);
    wire_len = append_preface_settings(wire, sizeof(wire), 0);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));

    /* when PRIORITY makes stream 5 depend on itself */
    priority.exclusive = false;
    priority.stream_dependency = 5u;
    priority.weight = 16u;
    wire_len = h2_frame_encode_priority(wire, sizeof(wire), 5u, &priority);
    EXPECT_TRUE(wire_len > 0u);

    /* then the dependency is rejected */
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_PROTOCOL_ERROR);
    return 0;
}

static int test_e2e_monotonic_stream_id_enforced(void)
{
    h2_connection conn;
    uint8_t wire[256];
    size_t wire_len;

    /* given an established connection that has accepted stream 3 */
    h2_connection_init(&conn);
    wire_len = append_preface_settings(wire, sizeof(wire), 0);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));
    wire_len = append_get_root(wire, sizeof(wire), 1u);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));
    wire_len = append_get_root(wire, sizeof(wire), 3u);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));

    /* when stream 1 is opened after stream 3 */
    wire_len = append_get_root(wire, sizeof(wire), 1u);
    EXPECT_TRUE(wire_len > 0u);

    /* then stream IDs are enforced as monotonically increasing */
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_PROTOCOL_ERROR);
    return 0;
}

int main(void)
{
    EXPECT_EQ_INT(test_e2e_200_stream_rollover(), 0);
    EXPECT_EQ_INT(test_e2e_malformed_hpack_integer_rejected(), 0);
    EXPECT_EQ_INT(test_e2e_blocked_window_defers_data(), 0);
    EXPECT_EQ_INT(test_e2e_priority_self_dependency_rejected(), 0);
    EXPECT_EQ_INT(test_e2e_monotonic_stream_id_enforced(), 0);
    puts("e2e_test: ok");
    return 0;
}
