#include "http2/hpack.h"
#include "http2/hpack_static.h"

#include <stdio.h>
#include <string.h>

#define EXPECT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)
#define EXPECT_EQ_INT(got, want) EXPECT_TRUE((int)(got) == (int)(want))
#define EXPECT_EQ_SIZE(got, want) EXPECT_TRUE((size_t)(got) == (size_t)(want))

static int test_static_table(void)
{
    const h2_hpack_static_entry *entry;

    /* given RFC 7541 Appendix A static table indexes */
    /* when looking up common pseudo-headers */
    entry = h2_hpack_static_get(8u);

    /* then index 8 is :status 200 */
    EXPECT_TRUE(entry != NULL);
    EXPECT_TRUE(strcmp(entry->name, ":status") == 0);
    EXPECT_TRUE(strcmp(entry->value, "200") == 0);
    EXPECT_TRUE(h2_hpack_static_get(61u) != NULL);
    EXPECT_TRUE(h2_hpack_static_get(62u) == NULL);
    return 0;
}

static int test_hpack_integer(void)
{
    uint8_t wire[8];
    uint32_t value;
    size_t used;
    size_t wire_len;

    /* given an HPACK integer larger than the prefix */
    /* when encoding and decoding it */
    wire_len = h2_hpack_encode_integer(wire, sizeof(wire), 5u, 0x20u, 1337u);
    EXPECT_TRUE(wire_len > 1u);
    EXPECT_EQ_INT(h2_hpack_decode_integer(wire, wire_len, 5u, &value, &used), H2_OK);

    /* then value and consumed length round-trip */
    EXPECT_EQ_SIZE(used, wire_len);
    EXPECT_TRUE(value == 1337u);
    return 0;
}

static int test_hpack_integer_rejects_overlong_uint32_shift(void)
{
    const uint8_t wire[] = { 0xffu, 0x80u, 0x80u, 0x80u, 0x80u, 0x80u, 0x00u };
    uint32_t value;
    size_t used;

    /* given an HPACK integer continuation that would require a shift past uint32_t */
    /* when decoding it into the uint32_t API */
    /* then it is rejected before any oversized shift occurs */
    EXPECT_EQ_INT(h2_hpack_decode_integer(wire, sizeof(wire), 7u, &value, &used), H2_COMPRESSION_ERROR);
    return 0;
}

static int test_hpack_integer_rejects_uint32_accumulation_wrap_on_final_byte(void)
{
    const uint8_t wire[] = { 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x0fu };
    uint32_t value;
    size_t used;

    /* given an HPACK integer whose final continuation exceeds uint32_t {[http2-engineer]} */
    /* when decoding it through the uint32_t API {[http2-engineer]} */
    /* then it is rejected after the final addition instead of wrapping {[http2-engineer]} */
    EXPECT_EQ_INT(h2_hpack_decode_integer(wire, sizeof(wire), 7u, &value, &used), H2_COMPRESSION_ERROR);
    return 0;
}

static int test_hpack_integer_rejects_uint32_overflow_on_final_byte(void)
{
    const uint8_t wire[] = { 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x10u };
    uint32_t value;
    size_t used;

    /* given a final HPACK continuation byte whose shifted value alone exceeds uint32_t {[http2-engineer]} */
    /* when the decoder reaches that final byte at shift 28 {[http2-engineer]} */
    /* then it rejects the integer before it can wrap into a small value {[http2-engineer]} */
    EXPECT_EQ_INT(h2_hpack_decode_integer(wire, sizeof(wire), 7u, &value, &used), H2_COMPRESSION_ERROR);
    return 0;
}

static int test_hpack_refuses_oversized_literal_field(void)
{
    uint8_t block[H2_HEADER_VALUE_CAP + 32u];
    h2_header_field fields[1];
    size_t field_len;
    size_t block_len;
    size_t pos;
    size_t index_len;
    size_t value_len;
    char value[H2_HEADER_VALUE_CAP + 1u];

    /* given a literal header whose value exceeds the documented fixed field cap */
    memset(value, 'a', sizeof(value) - 1u);
    value[sizeof(value) - 1u] = '\0';
    pos = 0u;
    index_len = h2_hpack_encode_integer(block + pos, sizeof(block) - pos, 4u, 0x00u, 58u);
    EXPECT_TRUE(index_len > 0u);
    pos += index_len;
    value_len = h2_hpack_encode_string(block + pos, sizeof(block) - pos, value);
    EXPECT_TRUE(value_len > 0u);
    block_len = pos + value_len;

    /* when decoding the header list */
    /* then the stream is refused instead of treating the size limit as HPACK corruption */
    EXPECT_EQ_INT(h2_hpack_decode_headers(block, block_len, fields, 1u, &field_len), H2_REFUSED_STREAM);
    return 0;
}

static int test_hpack_decode_request_path(void)
{
    uint8_t block[64];
    char path[64];
    size_t pos;
    size_t chunk_len;

    /* given a static-table-only request header block */
    pos = 0u;
    chunk_len = h2_hpack_encode_indexed(block + pos, sizeof(block) - pos, 2u);
    EXPECT_TRUE(chunk_len > 0u);
    pos += chunk_len;
    chunk_len = h2_hpack_encode_indexed(block + pos, sizeof(block) - pos, 6u);
    EXPECT_TRUE(chunk_len > 0u);
    pos += chunk_len;
    chunk_len = h2_hpack_encode_indexed(block + pos, sizeof(block) - pos, 4u);
    EXPECT_TRUE(chunk_len > 0u);
    pos += chunk_len;

    /* when extracting :path */
    EXPECT_EQ_INT(h2_hpack_extract_path(block, pos, path, sizeof(path)), H2_OK);

    /* then the static :path / value is returned */
    EXPECT_TRUE(strcmp(path, "/") == 0);
    return 0;
}

static int test_hpack_decode_literal(void)
{
    uint8_t block[128];
    h2_header_field fields[2];
    size_t field_len;
    size_t block_len;

    /* given a literal header with a new name */
    block_len = h2_hpack_encode_literal_new_name(block, sizeof(block), "server", "c11-h2");
    EXPECT_TRUE(block_len > 0u);

    /* when decoding the header block */
    EXPECT_EQ_INT(h2_hpack_decode_headers(block, block_len, fields, 2u, &field_len), H2_OK);

    /* then name and value round-trip */
    EXPECT_EQ_SIZE(field_len, 1u);
    EXPECT_TRUE(strcmp(fields[0].name, "server") == 0);
    EXPECT_TRUE(strcmp(fields[0].value, "c11-h2") == 0);
    return 0;
}

static int test_hpack_rejects_oversized_non_path_literal_value(void)
{
    uint8_t block[H2_HEADER_VALUE_CAP + 64u];
    char value[H2_HEADER_VALUE_CAP + 1u];
    char path[64];
    size_t pos;
    size_t chunk_len;

    /* given a header block with :path followed by an oversized non-:path literal value {[http2-engineer]} */
    pos = 0u;
    chunk_len = h2_hpack_encode_indexed(block + pos, sizeof(block) - pos, 4u);
    EXPECT_TRUE(chunk_len > 0u);
    pos += chunk_len;
    memset(value, 'x', sizeof(value) - 1u);
    value[sizeof(value) - 1u] = '\0';
    chunk_len = h2_hpack_encode_integer(block + pos, sizeof(block) - pos, 4u, 0x00u, 58u);
    EXPECT_TRUE(chunk_len > 0u);
    pos += chunk_len;
    chunk_len = h2_hpack_encode_string(block + pos, sizeof(block) - pos, value);
    EXPECT_TRUE(chunk_len > 0u);
    pos += chunk_len;

    /* when extracting :path scans the later literal field {[http2-engineer]} */
    /* then the oversized value is rejected instead of skipped unbounded {[http2-engineer]} */
    EXPECT_EQ_INT(h2_hpack_extract_path(block, pos, path, sizeof(path)), H2_REFUSED_STREAM);
    return 0;
}

int main(void)
{
    EXPECT_EQ_INT(test_static_table(), 0);
    EXPECT_EQ_INT(test_hpack_integer(), 0);
    EXPECT_EQ_INT(test_hpack_integer_rejects_overlong_uint32_shift(), 0);
    EXPECT_EQ_INT(test_hpack_integer_rejects_uint32_accumulation_wrap_on_final_byte(), 0);
    EXPECT_EQ_INT(test_hpack_integer_rejects_uint32_overflow_on_final_byte(), 0);
    EXPECT_EQ_INT(test_hpack_decode_request_path(), 0);
    EXPECT_EQ_INT(test_hpack_decode_literal(), 0);
    EXPECT_EQ_INT(test_hpack_refuses_oversized_literal_field(), 0);
    EXPECT_EQ_INT(test_hpack_rejects_oversized_non_path_literal_value(), 0);
    puts("hpack_test: ok");
    return 0;
}
