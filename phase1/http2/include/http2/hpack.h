#ifndef HTTP2_HPACK_H
#define HTTP2_HPACK_H

#include "http2/frame.h"

#include <stddef.h>
#include <stdint.h>

#define H2_HEADER_NAME_CAP 64u
#define H2_HEADER_VALUE_CAP 256u

typedef struct h2_header_field {
    char name[H2_HEADER_NAME_CAP];
    char value[H2_HEADER_VALUE_CAP];
} h2_header_field;

int h2_hpack_decode_integer(const uint8_t *wire, size_t wire_len, uint8_t prefix_bits, uint32_t *value, size_t *used);
size_t h2_hpack_encode_integer(uint8_t *wire, size_t cap, uint8_t prefix_bits, uint8_t high_bits, uint32_t value);
size_t h2_hpack_encode_indexed(uint8_t *wire, size_t cap, uint32_t static_index);
size_t h2_hpack_encode_string(uint8_t *wire, size_t cap, const char *text);
size_t h2_hpack_encode_literal_new_name(uint8_t *wire, size_t cap, const char *name, const char *value);
int h2_hpack_decode_headers(const uint8_t *wire, size_t wire_len, h2_header_field *fields, size_t field_cap, size_t *field_len);
int h2_hpack_extract_path(const uint8_t *wire, size_t wire_len, char *path, size_t path_cap);

#endif
