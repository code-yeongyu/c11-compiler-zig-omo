#include "http2/connection.h"
#include "http2/hpack.h"

#include <stdio.h>
#include <string.h>

#define EXPECT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)
#define EXPECT_EQ_INT(got, want) EXPECT_TRUE((int)(got) == (int)(want))
#define EXPECT_EQ_SIZE(got, want) EXPECT_TRUE((size_t)(got) == (size_t)(want))
#define EXPECT_EQ_U32(got, want) EXPECT_TRUE((uint32_t)(got) == (uint32_t)(want))

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

static size_t append_preface_and_settings(uint8_t *wire, size_t cap, int include_initial_window, uint32_t initial_window)
{
    h2_setting settings[2];
    size_t settings_len;
    size_t wire_pos;
    size_t chunk_len;

    if (cap < H2_CLIENT_PREFACE_LEN) {
        return 0u;
    }
    wire_pos = 0u;
    memcpy(wire + wire_pos, H2_CLIENT_PREFACE, H2_CLIENT_PREFACE_LEN);
    wire_pos += H2_CLIENT_PREFACE_LEN;

    settings[0].id = H2_SETTINGS_ENABLE_PUSH;
    settings[0].value = 0u;
    settings_len = 1u;
    if (include_initial_window) {
        settings[1].id = H2_SETTINGS_INITIAL_WINDOW_SIZE;
        settings[1].value = initial_window;
        settings_len = 2u;
    }
    chunk_len = h2_frame_encode_settings(wire + wire_pos, cap - wire_pos, 0u, settings, settings_len);
    if (chunk_len == 0u) {
        return 0u;
    }
    return wire_pos + chunk_len;
}

static size_t append_request_headers(uint8_t *wire, size_t cap, uint32_t stream_id, const char *path)
{
    uint8_t header_block[512];
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
    if (strcmp(path, "/") == 0) {
        chunk_len = h2_hpack_encode_indexed(header_block + pos, sizeof(header_block) - pos, 4u);
    } else {
        chunk_len = h2_hpack_encode_integer(header_block + pos, sizeof(header_block) - pos, 4u, 0x00u, 4u);
        if (chunk_len == 0u) {
            return 0u;
        }
        pos += chunk_len;
        chunk_len = h2_hpack_encode_string(header_block + pos, sizeof(header_block) - pos, path);
    }
    if (chunk_len == 0u) {
        return 0u;
    }
    pos += chunk_len;
    return h2_frame_encode_headers(wire, cap, stream_id, H2_FLAG_END_HEADERS | H2_FLAG_END_STREAM, header_block, pos);
}

static int output_has_frame_type(const h2_connection *conn, uint8_t frame_type)
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

static int output_has_data_body(const h2_connection *conn, const char *body, size_t body_len)
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
        if (header.type == H2_FRAME_DATA) {
            h2_data_payload payload;

            if (h2_frame_parse_data(&header, out + pos, &payload) != H2_OK) {
                return -1;
            }
            if (payload.data_len == body_len && memcmp(payload.data, body, body_len) == 0) {
                return 1;
            }
        }
        pos += header.length;
    }
    return 0;
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

static int test_connection_reclaims_closed_stream_slots(void)
{
    h2_connection conn;
    uint8_t wire[1024];
    size_t wire_len;
    uint32_t stream_id;
    int index;

    /* given a connection that has completed its preface and settings exchange */
    h2_connection_init(&conn);
    wire_len = append_preface_and_settings(wire, sizeof(wire), 0, 0u);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));

    /* when 200 sequential request streams are opened and closed */
    stream_id = 1u;
    for (index = 0; index < 200; index++) {
        wire_len = append_request_headers(wire, sizeof(wire), stream_id, "/");
        EXPECT_TRUE(wire_len > 0u);
        EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);

        /* then every stream, including the 129th, gets a DATA response */
        EXPECT_EQ_INT(output_has_data_body(&conn, "/\n", 2u), 1);
        h2_connection_consume_output(&conn, h2_connection_output_len(&conn));
        stream_id += 2u;
    }
    EXPECT_EQ_U32(conn.responses_sent, 200u);
    return 0;
}

static int test_connection_defers_blocked_response_data_until_window_update(void)
{
    h2_connection conn;
    uint8_t wire[256];
    size_t wire_len;

    /* given a request stream created under a zero initial send window */
    h2_connection_init(&conn);
    wire_len = append_preface_and_settings(wire, sizeof(wire), 1, 0u);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));
    wire_len = append_request_headers(wire, sizeof(wire), 1u, "/");
    EXPECT_TRUE(wire_len > 0u);

    /* when the server queues the response while DATA is flow-control blocked */
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);

    /* then response HEADERS are available but DATA is deferred instead of terminating the connection */
    EXPECT_EQ_INT(output_has_frame_type(&conn, H2_FRAME_HEADERS), 1);
    EXPECT_EQ_INT(output_has_frame_type(&conn, H2_FRAME_DATA), 0);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));

    /* when a stream WINDOW_UPDATE opens enough budget */
    wire_len = h2_frame_encode_window_update(wire, sizeof(wire), 1u, 2u);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);

    /* then the deferred DATA frame is sent */
    EXPECT_EQ_INT(output_has_data_body(&conn, "/\n", 2u), 1);
    EXPECT_EQ_U32(conn.responses_sent, 1u);
    return 0;
}

static int test_connection_rejects_non_monotonic_stream_id(void)
{
    h2_connection conn;
    uint8_t wire[256];
    size_t wire_len;

    /* given a connection that has already accepted stream 3 */
    h2_connection_init(&conn);
    wire_len = append_preface_and_settings(wire, sizeof(wire), 0, 0u);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));
    wire_len = append_request_headers(wire, sizeof(wire), 1u, "/");
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));
    wire_len = append_request_headers(wire, sizeof(wire), 3u, "/");
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));

    /* when a lower-numbered client stream is opened later */
    wire_len = append_request_headers(wire, sizeof(wire), 1u, "/");
    EXPECT_TRUE(wire_len > 0u);

    /* then the connection rejects the non-monotonic stream id */
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_PROTOCOL_ERROR);
    return 0;
}

static int test_connection_treats_rst_stream_on_idle_as_protocol_error(void)
{
    h2_connection conn;
    uint8_t wire[256];
    size_t wire_len;
    size_t pos;

    /* given a connection with no opened stream 99 {[http2-engineer]} */
    h2_connection_init(&conn);
    wire_len = append_preface_and_settings(wire, sizeof(wire), 0, 0u);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));

    /* when RST_STREAM arrives for that unopened stream {[http2-engineer]} */
    wire_len = h2_frame_encode_rst_stream(wire, sizeof(wire), 99u, H2_CANCEL);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_PROTOCOL_ERROR);

    /* then no ghost stream slot is synthesized and the connection is failed {[http2-engineer]} */
    for (pos = 0u; pos < H2_CONN_MAX_STREAMS; pos++) {
        EXPECT_TRUE(conn.streams[pos].id != 99u);
    }
    EXPECT_EQ_INT(output_has_frame_type(&conn, H2_FRAME_GOAWAY), 1);
    return 0;
}

static int test_connection_rejects_data_on_idle_stream_as_connection_error(void)
{
    h2_connection conn;
    uint8_t wire[256];
    size_t wire_len;
    const uint8_t data[] = { 'x' };

    /* given an established connection with no opened stream 1 */
    h2_connection_init(&conn);
    wire_len = append_preface_and_settings(wire, sizeof(wire), 0, 0u);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));

    /* when DATA arrives on the idle stream */
    wire_len = h2_frame_encode_data(wire, sizeof(wire), 1u, 0u, data, sizeof(data));
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_PROTOCOL_ERROR);

    /* then the peer gets GOAWAY rather than a stream-level RST_STREAM */
    EXPECT_EQ_INT(output_has_frame_type(&conn, H2_FRAME_GOAWAY), 1);
    EXPECT_EQ_INT(output_has_frame_type(&conn, H2_FRAME_RST_STREAM), 0);
    return 0;
}

static int test_connection_rejects_data_on_closed_stream_without_window_leak(void)
{
    h2_connection conn;
    uint8_t wire[256];
    size_t wire_len;
    const uint8_t data[] = { 'x' };
    int32_t recv_window;

    /* given stream 1 is known and closed after a complete request/response */
    h2_connection_init(&conn);
    wire_len = append_preface_and_settings(wire, sizeof(wire), 0, 0u);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));
    wire_len = append_request_headers(wire, sizeof(wire), 1u, "/");
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));
    recv_window = conn.conn_recv_window;

    /* when DATA arrives later for that closed-known stream */
    wire_len = h2_frame_encode_data(wire, sizeof(wire), 1u, 0u, data, sizeof(data));
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);

    /* then it is reset as a closed stream without consuming connection flow-control */
    EXPECT_EQ_INT(output_has_frame_type(&conn, H2_FRAME_RST_STREAM), 1);
    EXPECT_EQ_INT(conn.conn_recv_window, recv_window);
    return 0;
}

static int test_connection_reclaims_refused_stream_slots_after_rst_stream(void)
{
    h2_connection conn;
    uint8_t wire[1024];
    size_t wire_len;
    char path[H2_STREAM_PATH_CAP + 1u];
    uint32_t stream_id;
    int index;

    /* given an established connection and requests whose :path exceeds the stream cap {[http2-engineer]} */
    memset(path, 'a', sizeof(path) - 1u);
    path[0] = '/';
    path[sizeof(path) - 1u] = '\0';
    h2_connection_init(&conn);
    wire_len = append_preface_and_settings(wire, sizeof(wire), 0, 0u);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));

    /* when more refused streams arrive than the slot table can hold {[http2-engineer]} */
    stream_id = 1u;
    for (index = 0; index < 140; index++) {
        wire_len = append_request_headers(wire, sizeof(wire), stream_id, path);
        EXPECT_TRUE(wire_len > 0u);
        EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);

        /* then each refused stream is reset and its slot is reusable {[http2-engineer]} */
        EXPECT_EQ_INT(output_has_frame_type(&conn, H2_FRAME_RST_STREAM), 1);
        h2_connection_consume_output(&conn, h2_connection_output_len(&conn));
        stream_id += 2u;
    }
    return 0;
}

static int test_connection_refuses_oversized_path_as_stream_error(void)
{
    h2_connection conn;
    uint8_t wire[1024];
    size_t wire_len;
    char path[H2_STREAM_PATH_CAP + 1u];

    /* given a request with a :path field exceeding the documented fixed cap */
    memset(path, 'a', sizeof(path) - 1u);
    path[0] = '/';
    path[sizeof(path) - 1u] = '\0';
    h2_connection_init(&conn);
    wire_len = append_preface_and_settings(wire, sizeof(wire), 0, 0u);
    EXPECT_TRUE(wire_len > 0u);
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);
    h2_connection_consume_output(&conn, h2_connection_output_len(&conn));
    wire_len = append_request_headers(wire, sizeof(wire), 1u, path);
    EXPECT_TRUE(wire_len > 0u);

    /* when the HEADERS frame is processed */
    EXPECT_EQ_INT(h2_connection_feed(&conn, wire, wire_len), H2_OK);

    /* then the stream is refused with RST_STREAM rather than a connection compression error */
    EXPECT_EQ_INT(output_has_frame_type(&conn, H2_FRAME_RST_STREAM), 1);
    EXPECT_EQ_INT(output_has_frame_type(&conn, H2_FRAME_GOAWAY), 0);
    return 0;
}

int main(void)
{
    EXPECT_EQ_INT(test_connection_serves_path(), 0);
    EXPECT_EQ_INT(test_connection_rejects_bad_preface(), 0);
    EXPECT_EQ_INT(test_connection_reclaims_closed_stream_slots(), 0);
    EXPECT_EQ_INT(test_connection_defers_blocked_response_data_until_window_update(), 0);
    EXPECT_EQ_INT(test_connection_rejects_non_monotonic_stream_id(), 0);
    EXPECT_EQ_INT(test_connection_treats_rst_stream_on_idle_as_protocol_error(), 0);
    EXPECT_EQ_INT(test_connection_rejects_data_on_idle_stream_as_connection_error(), 0);
    EXPECT_EQ_INT(test_connection_rejects_data_on_closed_stream_without_window_leak(), 0);
    EXPECT_EQ_INT(test_connection_refuses_oversized_path_as_stream_error(), 0);
    EXPECT_EQ_INT(test_connection_reclaims_refused_stream_slots_after_rst_stream(), 0);
    puts("connection_test: ok");
    return 0;
}
