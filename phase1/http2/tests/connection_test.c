#include "http2/connection.h"
#include "http2/hpack.h"

#include <stdio.h>
#include <string.h>

#define EXPECT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)
#define EXPECT_EQ_INT(got, want) EXPECT_TRUE((int)(got) == (int)(want))
#define EXPECT_EQ_SIZE(got, want) EXPECT_TRUE((size_t)(got) == (size_t)(want))

static size_t append_request(uint8_t *wire, size_t cap)
{
    h2_setting settings[1];
    uint8_t header_block[32];
    size_t pos;
    size_t chunk_len;
    size_t wire_pos;

    wire_pos = 0u;
    memcpy(wire + wire_pos, H2_CLIENT_PREFACE, H2_CLIENT_PREFACE_LEN);
    wire_pos += H2_CLIENT_PREFACE_LEN;

    settings[0].id = H2_SETTINGS_ENABLE_PUSH;
    settings[0].value = 0u;
    chunk_len = h2_frame_encode_settings(wire + wire_pos, cap - wire_pos, 0u, settings, 1u);
    if (chunk_len == 0u) {
        return 0u;
    }
    wire_pos += chunk_len;

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
    chunk_len = h2_frame_encode_headers(wire + wire_pos, cap - wire_pos, 1u, H2_FLAG_END_HEADERS | H2_FLAG_END_STREAM, header_block, pos);
    if (chunk_len == 0u) {
        return 0u;
    }
    wire_pos += chunk_len;
    return wire_pos;
}

static int test_connection_serves_path(void)
{
    h2_connection conn;
    uint8_t request[256];
    h2_frame_header header;
    size_t request_len;
    size_t pos;
    const uint8_t *out;
    size_t out_len;
    int data_seen;

    /* given a client preface, SETTINGS, and GET / HEADERS */
    h2_connection_init(&conn);
    request_len = append_request(request, sizeof(request));
    EXPECT_TRUE(request_len > 0u);

    /* when feeding the connection state machine */
    EXPECT_EQ_INT(h2_connection_feed(&conn, request, request_len), H2_OK);

    /* then it queues server settings, ACK, response headers, and DATA echoing :path */
    out = h2_connection_output(&conn);
    out_len = h2_connection_output_len(&conn);
    EXPECT_TRUE(out != NULL);
    EXPECT_TRUE(out_len >= H2_FRAME_HEADER_LEN);
    data_seen = 0;
    pos = 0u;
    while (pos + H2_FRAME_HEADER_LEN <= out_len) {
        EXPECT_EQ_INT(h2_frame_parse_header(out + pos, out_len - pos, &header), H2_OK);
        pos += H2_FRAME_HEADER_LEN;
        EXPECT_TRUE(pos + header.length <= out_len);
        if (header.type == H2_FRAME_DATA) {
            h2_data_payload payload;

            EXPECT_EQ_INT(h2_frame_parse_data(&header, out + pos, &payload), H2_OK);
            EXPECT_EQ_SIZE(payload.data_len, 2u);
            EXPECT_TRUE(memcmp(payload.data, "/\n", 2u) == 0);
            data_seen = 1;
        }
        pos += header.length;
    }
    EXPECT_TRUE(data_seen == 1);
    return 0;
}

static int test_connection_rejects_bad_preface(void)
{
    h2_connection conn;
    uint8_t bad[H2_CLIENT_PREFACE_LEN];

    /* given bytes that are not the HTTP/2 client preface */
    h2_connection_init(&conn);
    memcpy(bad, H2_CLIENT_PREFACE, sizeof(bad));
    bad[10] = '1';

    /* when feeding them to the connection */
    EXPECT_EQ_INT(h2_connection_feed(&conn, bad, sizeof(bad)), H2_PROTOCOL_ERROR);

    /* then a GOAWAY is queued */
    EXPECT_TRUE(h2_connection_wants_write(&conn));
    return 0;
}

int main(void)
{
    EXPECT_EQ_INT(test_connection_serves_path(), 0);
    EXPECT_EQ_INT(test_connection_rejects_bad_preface(), 0);
    puts("connection_test: ok");
    return 0;
}
