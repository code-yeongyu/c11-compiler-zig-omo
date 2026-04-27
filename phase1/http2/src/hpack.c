#include "http2/hpack.h"

#include "http2/hpack_static.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>

static int h2_copy_text(char *dst, size_t dst_cap, const char *src)
{
    size_t src_len;

    if (dst == NULL || src == NULL || dst_cap == 0u) {
        return H2_COMPRESSION_ERROR;
    }
    src_len = strlen(src);
    if (src_len >= dst_cap) {
        return H2_REFUSED_STREAM;
    }
    memcpy(dst, src, src_len + 1u);
    return H2_OK;
}

int h2_hpack_decode_integer(const uint8_t *wire, size_t wire_len, uint8_t prefix_bits, uint32_t *value, size_t *used)
{
    uint32_t prefix_max;
    uint32_t result;
    uint32_t shift;
    size_t pos;

    if (wire == NULL || value == NULL || used == NULL || wire_len == 0u || prefix_bits == 0u || prefix_bits > 8u) {
        return H2_COMPRESSION_ERROR;
    }
    prefix_max = (1u << prefix_bits) - 1u;
    result = (uint32_t)(wire[0] & (uint8_t)prefix_max);
    if (result < prefix_max) {
        *value = result;
        *used = 1u;
        return H2_OK;
    }

    shift = 0u;
    pos = 1u;
    while (pos < wire_len) {
        uint8_t byte;

        byte = wire[pos];
        if (shift >= 32u) {
            return H2_COMPRESSION_ERROR;
        }
        if (shift >= 28u && (byte & 0x7fu) > 15u) {
            return H2_COMPRESSION_ERROR;
        }
        {
            uint32_t addition;

            addition = ((uint32_t)(byte & 0x7fu)) << shift;
            if (addition > UINT32_MAX - result) {
                return H2_COMPRESSION_ERROR;
            }
            result += addition;
        }
        pos++;
        if ((byte & 0x80u) == 0u) {
            *value = result;
            *used = pos;
            return H2_OK;
        }
        shift += 7u;
    }
    return H2_COMPRESSION_ERROR;
}

size_t h2_hpack_encode_integer(uint8_t *wire, size_t cap, uint8_t prefix_bits, uint8_t high_bits, uint32_t value)
{
    uint32_t prefix_max;
    size_t pos;

    if (wire == NULL || cap == 0u || prefix_bits == 0u || prefix_bits > 8u) {
        return 0u;
    }
    prefix_max = (1u << prefix_bits) - 1u;
    if (value < prefix_max) {
        wire[0] = (uint8_t)(high_bits | (uint8_t)value);
        return 1u;
    }

    wire[0] = (uint8_t)(high_bits | (uint8_t)prefix_max);
    value -= prefix_max;
    pos = 1u;
    while (value >= 128u) {
        if (pos >= cap) {
            return 0u;
        }
        wire[pos] = (uint8_t)((value & 0x7fu) | 0x80u);
        value >>= 7u;
        pos++;
    }
    if (pos >= cap) {
        return 0u;
    }
    wire[pos] = (uint8_t)value;
    return pos + 1u;
}

size_t h2_hpack_encode_indexed(uint8_t *wire, size_t cap, uint32_t static_index)
{
    if (h2_hpack_static_get(static_index) == NULL) {
        return 0u;
    }
    return h2_hpack_encode_integer(wire, cap, 7u, 0x80u, static_index);
}

size_t h2_hpack_encode_string(uint8_t *wire, size_t cap, const char *text)
{
    size_t text_len;
    size_t prefix_len;

    if (wire == NULL || text == NULL) {
        return 0u;
    }
    text_len = strlen(text);
    if (text_len > UINT32_MAX) {
        return 0u;
    }
    prefix_len = h2_hpack_encode_integer(wire, cap, 7u, 0x00u, (uint32_t)text_len);
    if (prefix_len == 0u || prefix_len + text_len > cap) {
        return 0u;
    }
    memcpy(wire + prefix_len, text, text_len);
    return prefix_len + text_len;
}

size_t h2_hpack_encode_literal_new_name(uint8_t *wire, size_t cap, const char *name, const char *value)
{
    size_t pos;
    size_t chunk_len;

    if (wire == NULL || cap == 0u || name == NULL || value == NULL) {
        return 0u;
    }
    wire[0] = 0x00u;
    pos = 1u;
    chunk_len = h2_hpack_encode_string(wire + pos, cap - pos, name);
    if (chunk_len == 0u) {
        return 0u;
    }
    pos += chunk_len;
    chunk_len = h2_hpack_encode_string(wire + pos, cap - pos, value);
    if (chunk_len == 0u) {
        return 0u;
    }
    return pos + chunk_len;
}

static int h2_hpack_decode_string(const uint8_t *wire, size_t wire_len, char *out, size_t out_cap, size_t *used)
{
    uint32_t value_len;
    size_t prefix_len;

    if (wire == NULL || out == NULL || used == NULL || out_cap == 0u || wire_len == 0u) {
        return H2_COMPRESSION_ERROR;
    }
    if ((wire[0] & 0x80u) != 0u) {
        return H2_COMPRESSION_ERROR;
    }
    if (h2_hpack_decode_integer(wire, wire_len, 7u, &value_len, &prefix_len) != H2_OK) {
        return H2_COMPRESSION_ERROR;
    }
    if (prefix_len + value_len > wire_len) {
        return H2_COMPRESSION_ERROR;
    }
    if ((size_t)value_len >= out_cap) {
        return H2_REFUSED_STREAM;
    }
    memcpy(out, wire + prefix_len, value_len);
    out[value_len] = '\0';
    *used = prefix_len + value_len;
    return H2_OK;
}

static int h2_hpack_skip_string(const uint8_t *wire, size_t wire_len, size_t *used)
{
    uint32_t value_len;
    size_t prefix_len;

    if (wire == NULL || used == NULL || wire_len == 0u) {
        return H2_COMPRESSION_ERROR;
    }
    if (h2_hpack_decode_integer(wire, wire_len, 7u, &value_len, &prefix_len) != H2_OK) {
        return H2_COMPRESSION_ERROR;
    }
    if (prefix_len + value_len > wire_len) {
        return H2_COMPRESSION_ERROR;
    }
    *used = prefix_len + value_len;
    return H2_OK;
}

static int h2_hpack_decode_indexed_literal_name(const uint8_t *wire, size_t wire_len, uint8_t prefix_bits, char *name, size_t name_cap, size_t *pos)
{
    uint32_t name_index;
    size_t used;
    int ret;

    if (h2_hpack_decode_integer(wire + *pos, wire_len - *pos, prefix_bits, &name_index, &used) != H2_OK) {
        return H2_COMPRESSION_ERROR;
    }
    *pos += used;
    if (name_index == 0u) {
        ret = h2_hpack_decode_string(wire + *pos, wire_len - *pos, name, name_cap, &used);
        if (ret != H2_OK) {
            return ret;
        }
        *pos += used;
        return H2_OK;
    }
    if (h2_hpack_static_get(name_index) == NULL) {
        return H2_COMPRESSION_ERROR;
    }
    return h2_copy_text(name, name_cap, h2_hpack_static_get(name_index)->name);
}

int h2_hpack_decode_headers(const uint8_t *wire, size_t wire_len, h2_header_field *fields, size_t field_cap, size_t *field_len)
{
    size_t pos;
    size_t count;

    if (wire == NULL || fields == NULL || field_len == NULL) {
        return H2_COMPRESSION_ERROR;
    }
    pos = 0u;
    count = 0u;
    while (pos < wire_len) {
        const h2_hpack_static_entry *entry;
        uint32_t index;
        size_t used;

        if (count >= field_cap) {
            return H2_REFUSED_STREAM;
        }
        if ((wire[pos] & 0x80u) != 0u) {
            if (h2_hpack_decode_integer(wire + pos, wire_len - pos, 7u, &index, &used) != H2_OK) {
                return H2_COMPRESSION_ERROR;
            }
            entry = h2_hpack_static_get(index);
            if (entry == NULL) {
                return H2_COMPRESSION_ERROR;
            }
            if (h2_copy_text(fields[count].name, sizeof(fields[count].name), entry->name) != H2_OK || h2_copy_text(fields[count].value, sizeof(fields[count].value), entry->value) != H2_OK) {
                return H2_REFUSED_STREAM;
            }
            pos += used;
            count++;
            continue;
        }
        if ((wire[pos] & 0xe0u) == 0x20u) {
            if (h2_hpack_decode_integer(wire + pos, wire_len - pos, 5u, &index, &used) != H2_OK) {
                return H2_COMPRESSION_ERROR;
            }
            pos += used;
            continue;
        }
        if ((wire[pos] & 0x40u) != 0u) {
            int ret;

            ret = h2_hpack_decode_indexed_literal_name(wire, wire_len, 6u, fields[count].name, sizeof(fields[count].name), &pos);
            if (ret != H2_OK) {
                return ret;
            }
        } else {
            int ret;

            ret = h2_hpack_decode_indexed_literal_name(wire, wire_len, 4u, fields[count].name, sizeof(fields[count].name), &pos);
            if (ret != H2_OK) {
                return ret;
            }
        }
        {
            int ret;

            ret = h2_hpack_decode_string(wire + pos, wire_len - pos, fields[count].value, sizeof(fields[count].value), &used);
            if (ret != H2_OK) {
                return ret;
            }
        }
        pos += used;
        count++;
    }
    *field_len = count;
    return H2_OK;
}

static int h2_maybe_copy_path(const char *name, const char *value, char *path, size_t path_cap)
{
    if (strcmp(name, ":path") == 0) {
        return h2_copy_text(path, path_cap, value);
    }
    return H2_OK;
}

int h2_hpack_extract_path(const uint8_t *wire, size_t wire_len, char *path, size_t path_cap)
{
    size_t pos;

    if (wire == NULL || path == NULL || path_cap == 0u) {
        return H2_COMPRESSION_ERROR;
    }
    path[0] = '\0';
    pos = 0u;
    while (pos < wire_len) {
        const h2_hpack_static_entry *entry;
        uint32_t index;
        size_t used;
        char name[H2_HEADER_NAME_CAP];
        char value[H2_HEADER_VALUE_CAP];

        if ((wire[pos] & 0x80u) != 0u) {
            if (h2_hpack_decode_integer(wire + pos, wire_len - pos, 7u, &index, &used) != H2_OK) {
                return H2_COMPRESSION_ERROR;
            }
            entry = h2_hpack_static_get(index);
            if (entry == NULL) {
                return H2_COMPRESSION_ERROR;
            }
            int ret;

            ret = h2_maybe_copy_path(entry->name, entry->value, path, path_cap);
            if (ret != H2_OK) {
                return ret;
            }
            pos += used;
            continue;
        }
        if ((wire[pos] & 0xe0u) == 0x20u) {
            if (h2_hpack_decode_integer(wire + pos, wire_len - pos, 5u, &index, &used) != H2_OK) {
                return H2_COMPRESSION_ERROR;
            }
            pos += used;
            continue;
        }
        if ((wire[pos] & 0x40u) != 0u) {
            int ret;

            ret = h2_hpack_decode_indexed_literal_name(wire, wire_len, 6u, name, sizeof(name), &pos);
            if (ret != H2_OK) {
                return ret;
            }
        } else {
            int ret;

            ret = h2_hpack_decode_indexed_literal_name(wire, wire_len, 4u, name, sizeof(name), &pos);
            if (ret != H2_OK) {
                return ret;
            }
        }
        if (strcmp(name, ":path") == 0) {
            int ret;

            ret = h2_hpack_decode_string(wire + pos, wire_len - pos, value, sizeof(value), &used);
            if (ret != H2_OK) {
                return ret;
            }
            ret = h2_copy_text(path, path_cap, value);
            if (ret != H2_OK) {
                return ret;
            }
        } else if (h2_hpack_skip_string(wire + pos, wire_len - pos, &used) != H2_OK) {
            return H2_COMPRESSION_ERROR;
        }
        pos += used;
    }
    if (path[0] == '\0') {
        return H2_PROTOCOL_ERROR;
    }
    return H2_OK;
}
