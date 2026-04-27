#include "http2/frame.h"

#include <stdio.h>
#include <string.h>

#define EXPECT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)
#define EXPECT_EQ_U32(got, want) EXPECT_TRUE((uint32_t)(got) == (uint32_t)(want))
#define EXPECT_EQ_SIZE(got, want) EXPECT_TRUE((size_t)(got) == (size_t)(want))
#define EXPECT_EQ_INT(got, want) EXPECT_TRUE((int)(got) == (int)(want))

static int test_header_reserved_bit_cleared(void)
{
    uint8_t wire[H2_FRAME_HEADER_LEN] = { 0x00u, 0x00u, 0x00u, H2_FRAME_SETTINGS, 0x00u, 0x80u, 0x00u, 0x00u, 0x05u };
    h2_frame_header header;

    /* given a frame header with the stream-id reserved bit set */
    /* when parsing the common 9-octet frame header */
    EXPECT_EQ_INT(h2_frame_parse_header(wire, sizeof(wire), &header), H2_OK);

    /* then the reserved bit is ignored and cleared from stream_id */
    EXPECT_EQ_U32(header.stream_id, 5u);
    return 0;
}

static int test_data_frame(void)
{
    const uint8_t data[] = { 'a', 'b', 'c' };
    uint8_t wire[64];
    h2_frame_header header;
    h2_data_payload payload;
    size_t wire_len;

    /* given a DATA payload for stream 1 */
    /* when encoding and parsing the DATA frame */
    wire_len = h2_frame_encode_data(wire, sizeof(wire), 1u, H2_FLAG_END_STREAM, data, sizeof(data));
    EXPECT_EQ_SIZE(wire_len, H2_FRAME_HEADER_LEN + sizeof(data));
    EXPECT_EQ_INT(h2_frame_parse_header(wire, wire_len, &header), H2_OK);
    EXPECT_EQ_INT(h2_frame_parse_data(&header, wire + H2_FRAME_HEADER_LEN, &payload), H2_OK);

    /* then type, stream, flags, and payload round-trip */
    EXPECT_EQ_U32(header.type, H2_FRAME_DATA);
    EXPECT_EQ_U32(header.flags, H2_FLAG_END_STREAM);
    EXPECT_EQ_U32(header.stream_id, 1u);
    EXPECT_EQ_SIZE(payload.data_len, sizeof(data));
    EXPECT_TRUE(memcmp(payload.data, data, sizeof(data)) == 0);

    /* given DATA on stream 0 */
    /* when validating the frame */
    header.stream_id = 0u;

    /* then it is rejected as a protocol error */
    EXPECT_EQ_INT(h2_frame_validate_payload(&header), H2_PROTOCOL_ERROR);
    return 0;
}

static int test_headers_frame(void)
{
    const uint8_t block[] = { 0x82u, 0x84u };
    uint8_t wire[64];
    h2_frame_header header;
    h2_headers_payload payload;
    size_t wire_len;

    /* given a small HEADERS block */
    /* when encoding and parsing HEADERS */
    wire_len = h2_frame_encode_headers(wire, sizeof(wire), 3u, H2_FLAG_END_HEADERS, block, sizeof(block));
    EXPECT_EQ_SIZE(wire_len, H2_FRAME_HEADER_LEN + sizeof(block));
    EXPECT_EQ_INT(h2_frame_parse_header(wire, wire_len, &header), H2_OK);
    EXPECT_EQ_INT(h2_frame_parse_headers(&header, wire + H2_FRAME_HEADER_LEN, &payload), H2_OK);

    /* then the header block fragment round-trips */
    EXPECT_EQ_U32(header.type, H2_FRAME_HEADERS);
    EXPECT_EQ_U32(header.stream_id, 3u);
    EXPECT_EQ_SIZE(payload.header_block_len, sizeof(block));
    EXPECT_TRUE(memcmp(payload.header_block, block, sizeof(block)) == 0);

    /* given HEADERS with stream 0 */
    /* when validating it */
    header.stream_id = 0u;

    /* then it is rejected */
    EXPECT_EQ_INT(h2_frame_validate_payload(&header), H2_PROTOCOL_ERROR);
    return 0;
}

static int test_priority_frame(void)
{
    h2_priority_payload priority;
    h2_priority_payload parsed;
    uint8_t wire[64];
    h2_frame_header header;
    size_t wire_len;

    /* given a PRIORITY payload */
    priority.exclusive = true;
    priority.stream_dependency = 7u;
    priority.weight = 42u;

    /* when encoding and parsing PRIORITY */
    wire_len = h2_frame_encode_priority(wire, sizeof(wire), 5u, &priority);
    EXPECT_EQ_SIZE(wire_len, H2_FRAME_HEADER_LEN + 5u);
    EXPECT_EQ_INT(h2_frame_parse_header(wire, wire_len, &header), H2_OK);
    EXPECT_EQ_INT(h2_frame_parse_priority(&header, wire + H2_FRAME_HEADER_LEN, &parsed), H2_OK);

    /* then exclusive, dependency, and weight round-trip */
    EXPECT_TRUE(parsed.exclusive);
    EXPECT_EQ_U32(parsed.stream_dependency, 7u);
    EXPECT_EQ_U32(parsed.weight, 42u);

    /* given a PRIORITY header with bad length */
    /* when validating it */
    header.length = 4u;

    /* then it is rejected with FRAME_SIZE_ERROR */
    EXPECT_EQ_INT(h2_frame_validate_payload(&header), H2_FRAME_SIZE_ERROR);
    return 0;
}

static int test_priority_rejects_self_dependency(void)
{
    uint8_t payload[5] = { 0x00u, 0x00u, 0x00u, 0x05u, 0x10u };
    h2_frame_header header;
    h2_priority_payload parsed;

    /* given a PRIORITY frame whose stream depends on itself */
    header.length = 5u;
    header.type = H2_FRAME_PRIORITY;
    header.flags = 0u;
    header.stream_id = 5u;

    /* when parsing the priority payload */
    /* then the self-dependency is rejected as a protocol error */
    EXPECT_EQ_INT(h2_frame_parse_priority(&header, payload, &parsed), H2_PROTOCOL_ERROR);
    return 0;
}

static int test_encoder_rejects_unserialized_padding_and_priority_flags(void)
{
    const uint8_t data[] = { 'x' };
    uint8_t wire[64];

    /* given encoder calls with flags whose payload fields are not serialized yet */
    /* when asking the encoder to write PADDED DATA or PADDED/PRIORITY HEADERS */
    /* then it refuses to produce malformed wire bytes */
    EXPECT_EQ_SIZE(h2_frame_encode_data(wire, sizeof(wire), 1u, H2_FLAG_PADDED, data, sizeof(data)), 0u);
    EXPECT_EQ_SIZE(h2_frame_encode_headers(wire, sizeof(wire), 1u, H2_FLAG_PADDED, data, sizeof(data)), 0u);
    EXPECT_EQ_SIZE(h2_frame_encode_headers(wire, sizeof(wire), 1u, H2_FLAG_PRIORITY, data, sizeof(data)), 0u);
    EXPECT_EQ_SIZE(h2_frame_encode_push_promise(wire, sizeof(wire), 1u, H2_FLAG_PADDED, 2u, data, sizeof(data)), 0u);
    return 0;
}

static int test_rst_stream_frame(void)
{
    uint8_t wire[64];
    h2_frame_header header;
    uint32_t error_code;
    size_t wire_len;

    /* given an RST_STREAM error code */
    /* when encoding and parsing RST_STREAM */
    wire_len = h2_frame_encode_rst_stream(wire, sizeof(wire), 7u, H2_CANCEL);
    EXPECT_EQ_SIZE(wire_len, H2_FRAME_HEADER_LEN + 4u);
    EXPECT_EQ_INT(h2_frame_parse_header(wire, wire_len, &header), H2_OK);
    EXPECT_EQ_INT(h2_frame_parse_rst_stream(&header, wire + H2_FRAME_HEADER_LEN, &error_code), H2_OK);

    /* then the error code round-trips */
    EXPECT_EQ_U32(error_code, H2_CANCEL);

    /* given RST_STREAM with length 3 */
    /* when validating it */
    header.length = 3u;

    /* then it is rejected */
    EXPECT_EQ_INT(h2_frame_validate_payload(&header), H2_FRAME_SIZE_ERROR);
    return 0;
}

static int test_settings_frame(void)
{
    h2_setting settings[2];
    h2_setting parsed[2];
    uint8_t wire[64];
    h2_frame_header header;
    size_t parsed_len;
    size_t wire_len;

    /* given two SETTINGS entries */
    settings[0].id = H2_SETTINGS_ENABLE_PUSH;
    settings[0].value = 0u;
    settings[1].id = H2_SETTINGS_MAX_FRAME_SIZE;
    settings[1].value = H2_DEFAULT_MAX_FRAME_SIZE;

    /* when encoding and parsing SETTINGS */
    wire_len = h2_frame_encode_settings(wire, sizeof(wire), 0u, settings, 2u);
    EXPECT_EQ_SIZE(wire_len, H2_FRAME_HEADER_LEN + 12u);
    EXPECT_EQ_INT(h2_frame_parse_header(wire, wire_len, &header), H2_OK);
    EXPECT_EQ_INT(h2_frame_parse_settings(&header, wire + H2_FRAME_HEADER_LEN, parsed, 2u, &parsed_len), H2_OK);

    /* then the entries round-trip */
    EXPECT_EQ_SIZE(parsed_len, 2u);
    EXPECT_EQ_U32(parsed[0].id, H2_SETTINGS_ENABLE_PUSH);
    EXPECT_EQ_U32(parsed[0].value, 0u);
    EXPECT_EQ_U32(parsed[1].id, H2_SETTINGS_MAX_FRAME_SIZE);
    EXPECT_EQ_U32(parsed[1].value, H2_DEFAULT_MAX_FRAME_SIZE);

    /* given SETTINGS ACK with non-empty payload */
    /* when validating it */
    header.flags = H2_FLAG_ACK;
    header.length = 6u;

    /* then it is rejected */
    EXPECT_EQ_INT(h2_frame_validate_payload(&header), H2_FRAME_SIZE_ERROR);
    return 0;
}

static int test_push_promise_frame(void)
{
    const uint8_t block[] = { 0x84u };
    uint8_t wire[64];
    h2_frame_header header;
    h2_push_promise_payload payload;
    size_t wire_len;

    /* given a PUSH_PROMISE payload */
    /* when encoding and parsing PUSH_PROMISE */
    wire_len = h2_frame_encode_push_promise(wire, sizeof(wire), 1u, H2_FLAG_END_HEADERS, 2u, block, sizeof(block));
    EXPECT_EQ_SIZE(wire_len, H2_FRAME_HEADER_LEN + 5u);
    EXPECT_EQ_INT(h2_frame_parse_header(wire, wire_len, &header), H2_OK);
    EXPECT_EQ_INT(h2_frame_parse_push_promise(&header, wire + H2_FRAME_HEADER_LEN, &payload), H2_OK);

    /* then promised stream id and header block round-trip */
    EXPECT_EQ_U32(payload.promised_stream_id, 2u);
    EXPECT_EQ_SIZE(payload.header_block_len, sizeof(block));
    EXPECT_TRUE(memcmp(payload.header_block, block, sizeof(block)) == 0);

    /* given PUSH_PROMISE with too-short length */
    /* when validating it */
    header.length = 3u;

    /* then it is rejected */
    EXPECT_EQ_INT(h2_frame_validate_payload(&header), H2_FRAME_SIZE_ERROR);
    return 0;
}

static int test_ping_frame(void)
{
    const uint8_t opaque[8] = { 'p', 'i', 'n', 'g', 'd', 'a', 't', 'a' };
    uint8_t parsed[8];
    uint8_t wire[64];
    h2_frame_header header;
    size_t wire_len;

    /* given opaque PING bytes */
    /* when encoding and parsing PING */
    wire_len = h2_frame_encode_ping(wire, sizeof(wire), H2_FLAG_ACK, opaque);
    EXPECT_EQ_SIZE(wire_len, H2_FRAME_HEADER_LEN + 8u);
    EXPECT_EQ_INT(h2_frame_parse_header(wire, wire_len, &header), H2_OK);
    EXPECT_EQ_INT(h2_frame_parse_ping(&header, wire + H2_FRAME_HEADER_LEN, parsed), H2_OK);

    /* then the opaque bytes round-trip */
    EXPECT_TRUE(memcmp(parsed, opaque, sizeof(parsed)) == 0);

    /* given PING with length 7 */
    /* when validating it */
    header.length = 7u;

    /* then it is rejected */
    EXPECT_EQ_INT(h2_frame_validate_payload(&header), H2_FRAME_SIZE_ERROR);
    return 0;
}

static int test_goaway_frame(void)
{
    const uint8_t debug[] = { 'b', 'y', 'e' };
    uint8_t wire[64];
    h2_frame_header header;
    h2_goaway_payload payload;
    size_t wire_len;

    /* given a GOAWAY payload */
    /* when encoding and parsing GOAWAY */
    wire_len = h2_frame_encode_goaway(wire, sizeof(wire), 9u, H2_OK, debug, sizeof(debug));
    EXPECT_EQ_SIZE(wire_len, H2_FRAME_HEADER_LEN + 8u + sizeof(debug));
    EXPECT_EQ_INT(h2_frame_parse_header(wire, wire_len, &header), H2_OK);
    EXPECT_EQ_INT(h2_frame_parse_goaway(&header, wire + H2_FRAME_HEADER_LEN, &payload), H2_OK);

    /* then stream id, error code, and debug data round-trip */
    EXPECT_EQ_U32(payload.last_stream_id, 9u);
    EXPECT_EQ_U32(payload.error_code, H2_OK);
    EXPECT_EQ_SIZE(payload.debug_len, sizeof(debug));
    EXPECT_TRUE(memcmp(payload.debug_data, debug, sizeof(debug)) == 0);

    /* given GOAWAY with length 7 */
    /* when validating it */
    header.length = 7u;

    /* then it is rejected */
    EXPECT_EQ_INT(h2_frame_validate_payload(&header), H2_FRAME_SIZE_ERROR);
    return 0;
}

static int test_window_update_frame(void)
{
    uint8_t wire[64];
    h2_frame_header header;
    uint32_t increment;
    size_t wire_len;

    /* given a WINDOW_UPDATE increment */
    /* when encoding and parsing WINDOW_UPDATE */
    wire_len = h2_frame_encode_window_update(wire, sizeof(wire), 0u, 1024u);
    EXPECT_EQ_SIZE(wire_len, H2_FRAME_HEADER_LEN + 4u);
    EXPECT_EQ_INT(h2_frame_parse_header(wire, wire_len, &header), H2_OK);
    EXPECT_EQ_INT(h2_frame_parse_window_update(&header, wire + H2_FRAME_HEADER_LEN, &increment), H2_OK);

    /* then the increment round-trips */
    EXPECT_EQ_U32(increment, 1024u);

    /* given WINDOW_UPDATE with zero increment */
    /* when parsing it */
    wire[H2_FRAME_HEADER_LEN + 0u] = 0u;
    wire[H2_FRAME_HEADER_LEN + 1u] = 0u;
    wire[H2_FRAME_HEADER_LEN + 2u] = 0u;
    wire[H2_FRAME_HEADER_LEN + 3u] = 0u;

    /* then it is rejected */
    EXPECT_EQ_INT(h2_frame_parse_window_update(&header, wire + H2_FRAME_HEADER_LEN, &increment), H2_PROTOCOL_ERROR);
    return 0;
}

static int test_continuation_frame(void)
{
    const uint8_t block[] = { 0x40u, 0x00u, 0x00u };
    uint8_t wire[64];
    h2_frame_header header;
    h2_continuation_payload payload;
    size_t wire_len;

    /* given a CONTINUATION fragment */
    /* when encoding and parsing CONTINUATION */
    wire_len = h2_frame_encode_continuation(wire, sizeof(wire), 11u, H2_FLAG_END_HEADERS, block, sizeof(block));
    EXPECT_EQ_SIZE(wire_len, H2_FRAME_HEADER_LEN + sizeof(block));
    EXPECT_EQ_INT(h2_frame_parse_header(wire, wire_len, &header), H2_OK);
    EXPECT_EQ_INT(h2_frame_parse_continuation(&header, wire + H2_FRAME_HEADER_LEN, &payload), H2_OK);

    /* then the fragment round-trips */
    EXPECT_EQ_SIZE(payload.header_block_len, sizeof(block));
    EXPECT_TRUE(memcmp(payload.header_block, block, sizeof(block)) == 0);

    /* given CONTINUATION on stream 0 */
    /* when validating it */
    header.stream_id = 0u;

    /* then it is rejected */
    EXPECT_EQ_INT(h2_frame_validate_payload(&header), H2_PROTOCOL_ERROR);
    return 0;
}

int main(void)
{
    EXPECT_EQ_INT(test_header_reserved_bit_cleared(), 0);
    EXPECT_EQ_INT(test_data_frame(), 0);
    EXPECT_EQ_INT(test_headers_frame(), 0);
    EXPECT_EQ_INT(test_priority_frame(), 0);
    EXPECT_EQ_INT(test_priority_rejects_self_dependency(), 0);
    EXPECT_EQ_INT(test_encoder_rejects_unserialized_padding_and_priority_flags(), 0);
    EXPECT_EQ_INT(test_rst_stream_frame(), 0);
    EXPECT_EQ_INT(test_settings_frame(), 0);
    EXPECT_EQ_INT(test_push_promise_frame(), 0);
    EXPECT_EQ_INT(test_ping_frame(), 0);
    EXPECT_EQ_INT(test_goaway_frame(), 0);
    EXPECT_EQ_INT(test_window_update_frame(), 0);
    EXPECT_EQ_INT(test_continuation_frame(), 0);
    puts("frame_test: ok");
    return 0;
}
