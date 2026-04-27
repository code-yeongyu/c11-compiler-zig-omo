#ifndef HTTP2_FRAME_H
#define HTTP2_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define H2_FRAME_HEADER_LEN 9u
#define H2_DEFAULT_MAX_FRAME_SIZE 16384u
#define H2_MAX_FRAME_SIZE 16777215u
#define H2_DEFAULT_WINDOW_SIZE 65535

typedef enum h2_error_code {
    H2_OK = 0x0,
    H2_PROTOCOL_ERROR = 0x1,
    H2_INTERNAL_ERROR = 0x2,
    H2_FLOW_CONTROL_ERROR = 0x3,
    H2_SETTINGS_TIMEOUT = 0x4,
    H2_STREAM_CLOSED = 0x5,
    H2_FRAME_SIZE_ERROR = 0x6,
    H2_REFUSED_STREAM = 0x7,
    H2_CANCEL = 0x8,
    H2_COMPRESSION_ERROR = 0x9,
    H2_CONNECT_ERROR = 0xa,
    H2_ENHANCE_YOUR_CALM = 0xb,
    H2_INADEQUATE_SECURITY = 0xc,
    H2_HTTP_1_1_REQUIRED = 0xd
} h2_error_code;

typedef enum h2_frame_type {
    H2_FRAME_DATA = 0x0,
    H2_FRAME_HEADERS = 0x1,
    H2_FRAME_PRIORITY = 0x2,
    H2_FRAME_RST_STREAM = 0x3,
    H2_FRAME_SETTINGS = 0x4,
    H2_FRAME_PUSH_PROMISE = 0x5,
    H2_FRAME_PING = 0x6,
    H2_FRAME_GOAWAY = 0x7,
    H2_FRAME_WINDOW_UPDATE = 0x8,
    H2_FRAME_CONTINUATION = 0x9
} h2_frame_type;

enum h2_frame_flags {
    H2_FLAG_END_STREAM = 0x1,
    H2_FLAG_ACK = 0x1,
    H2_FLAG_END_HEADERS = 0x4,
    H2_FLAG_PADDED = 0x8,
    H2_FLAG_PRIORITY = 0x20
};

typedef enum h2_settings_id {
    H2_SETTINGS_HEADER_TABLE_SIZE = 0x1,
    H2_SETTINGS_ENABLE_PUSH = 0x2,
    H2_SETTINGS_MAX_CONCURRENT_STREAMS = 0x3,
    H2_SETTINGS_INITIAL_WINDOW_SIZE = 0x4,
    H2_SETTINGS_MAX_FRAME_SIZE = 0x5,
    H2_SETTINGS_MAX_HEADER_LIST_SIZE = 0x6
} h2_settings_id;

typedef struct h2_frame_header {
    uint32_t length;
    uint8_t type;
    uint8_t flags;
    uint32_t stream_id;
} h2_frame_header;

typedef struct h2_setting {
    uint16_t id;
    uint32_t value;
} h2_setting;

typedef struct h2_priority_payload {
    bool exclusive;
    uint32_t stream_dependency;
    uint8_t weight;
} h2_priority_payload;

typedef struct h2_data_payload {
    const uint8_t *data;
    size_t data_len;
    uint8_t pad_len;
} h2_data_payload;

typedef struct h2_headers_payload {
    const uint8_t *header_block;
    size_t header_block_len;
    uint8_t pad_len;
    bool has_priority;
    h2_priority_payload priority;
} h2_headers_payload;

typedef struct h2_push_promise_payload {
    uint32_t promised_stream_id;
    const uint8_t *header_block;
    size_t header_block_len;
    uint8_t pad_len;
} h2_push_promise_payload;

typedef struct h2_goaway_payload {
    uint32_t last_stream_id;
    uint32_t error_code;
    const uint8_t *debug_data;
    size_t debug_len;
} h2_goaway_payload;

typedef struct h2_continuation_payload {
    const uint8_t *header_block;
    size_t header_block_len;
} h2_continuation_payload;

int h2_frame_parse_header(const uint8_t *wire, size_t wire_len, h2_frame_header *out);
int h2_frame_write_header(uint8_t *wire, size_t cap, const h2_frame_header *header);
int h2_frame_validate_payload(const h2_frame_header *header);

int h2_frame_parse_data(const h2_frame_header *header, const uint8_t *payload, h2_data_payload *out);
int h2_frame_parse_headers(const h2_frame_header *header, const uint8_t *payload, h2_headers_payload *out);
int h2_frame_parse_priority(const h2_frame_header *header, const uint8_t *payload, h2_priority_payload *out);
int h2_frame_parse_settings(const h2_frame_header *header, const uint8_t *payload, h2_setting *settings, size_t max_settings, size_t *settings_len);
int h2_frame_parse_window_update(const h2_frame_header *header, const uint8_t *payload, uint32_t *increment);
int h2_frame_parse_ping(const h2_frame_header *header, const uint8_t *payload, uint8_t opaque[8]);
int h2_frame_parse_goaway(const h2_frame_header *header, const uint8_t *payload, h2_goaway_payload *out);
int h2_frame_parse_rst_stream(const h2_frame_header *header, const uint8_t *payload, uint32_t *error_code);
int h2_frame_parse_continuation(const h2_frame_header *header, const uint8_t *payload, h2_continuation_payload *out);
int h2_frame_parse_push_promise(const h2_frame_header *header, const uint8_t *payload, h2_push_promise_payload *out);

size_t h2_frame_encode_data(uint8_t *wire, size_t cap, uint32_t stream_id, uint8_t flags, const uint8_t *data, size_t data_len);
size_t h2_frame_encode_headers(uint8_t *wire, size_t cap, uint32_t stream_id, uint8_t flags, const uint8_t *header_block, size_t header_block_len);
size_t h2_frame_encode_priority(uint8_t *wire, size_t cap, uint32_t stream_id, const h2_priority_payload *priority);
size_t h2_frame_encode_rst_stream(uint8_t *wire, size_t cap, uint32_t stream_id, uint32_t error_code);
size_t h2_frame_encode_settings(uint8_t *wire, size_t cap, uint8_t flags, const h2_setting *settings, size_t settings_len);
size_t h2_frame_encode_push_promise(uint8_t *wire, size_t cap, uint32_t stream_id, uint8_t flags, uint32_t promised_stream_id, const uint8_t *header_block, size_t header_block_len);
size_t h2_frame_encode_ping(uint8_t *wire, size_t cap, uint8_t flags, const uint8_t opaque[8]);
size_t h2_frame_encode_goaway(uint8_t *wire, size_t cap, uint32_t last_stream_id, uint32_t error_code, const uint8_t *debug_data, size_t debug_len);
size_t h2_frame_encode_window_update(uint8_t *wire, size_t cap, uint32_t stream_id, uint32_t increment);
size_t h2_frame_encode_continuation(uint8_t *wire, size_t cap, uint32_t stream_id, uint8_t flags, const uint8_t *header_block, size_t header_block_len);

#endif
