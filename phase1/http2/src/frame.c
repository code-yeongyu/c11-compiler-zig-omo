#include "http2/frame.h"

#include <string.h>

static uint16_t h2_read_u16(const uint8_t *wire)
{
    return (uint16_t)(((uint16_t)wire[0] << 8u) | (uint16_t)wire[1]);
}

static uint32_t h2_read_u31(const uint8_t *wire)
{
    return (((uint32_t)wire[0] & 0x7fu) << 24u) |
        ((uint32_t)wire[1] << 16u) |
        ((uint32_t)wire[2] << 8u) |
        (uint32_t)wire[3];
}

static uint32_t h2_read_u32(const uint8_t *wire)
{
    return ((uint32_t)wire[0] << 24u) |
        ((uint32_t)wire[1] << 16u) |
        ((uint32_t)wire[2] << 8u) |
        (uint32_t)wire[3];
}

static void h2_write_u16(uint8_t *wire, uint16_t value)
{
    wire[0] = (uint8_t)(value >> 8u);
    wire[1] = (uint8_t)value;
}

static void h2_write_u32(uint8_t *wire, uint32_t value)
{
    wire[0] = (uint8_t)(value >> 24u);
    wire[1] = (uint8_t)(value >> 16u);
    wire[2] = (uint8_t)(value >> 8u);
    wire[3] = (uint8_t)value;
}

static int h2_validate_stream_nonzero(const h2_frame_header *header)
{
    return header->stream_id == 0u ? H2_PROTOCOL_ERROR : H2_OK;
}

int h2_frame_parse_header(const uint8_t *wire, size_t wire_len, h2_frame_header *out)
{
    if (wire == NULL || out == NULL || wire_len < H2_FRAME_HEADER_LEN) {
        return H2_FRAME_SIZE_ERROR;
    }

    out->length = ((uint32_t)wire[0] << 16u) | ((uint32_t)wire[1] << 8u) | (uint32_t)wire[2];
    out->type = wire[3];
    out->flags = wire[4];
    out->stream_id = h2_read_u31(wire + 5u);
    return H2_OK;
}

int h2_frame_write_header(uint8_t *wire, size_t cap, const h2_frame_header *header)
{
    if (wire == NULL || header == NULL || cap < H2_FRAME_HEADER_LEN || header->length > H2_MAX_FRAME_SIZE) {
        return H2_FRAME_SIZE_ERROR;
    }

    wire[0] = (uint8_t)(header->length >> 16u);
    wire[1] = (uint8_t)(header->length >> 8u);
    wire[2] = (uint8_t)header->length;
    wire[3] = header->type;
    wire[4] = header->flags;
    h2_write_u32(wire + 5u, header->stream_id & 0x7fffffffu);
    return H2_OK;
}

int h2_frame_validate_payload(const h2_frame_header *header)
{
    if (header == NULL || header->length > H2_MAX_FRAME_SIZE) {
        return H2_FRAME_SIZE_ERROR;
    }

    switch (header->type) {
    case H2_FRAME_DATA:
        return h2_validate_stream_nonzero(header);
    case H2_FRAME_HEADERS:
        return h2_validate_stream_nonzero(header);
    case H2_FRAME_PRIORITY:
        if (header->stream_id == 0u) {
            return H2_PROTOCOL_ERROR;
        }
        return header->length == 5u ? H2_OK : H2_FRAME_SIZE_ERROR;
    case H2_FRAME_RST_STREAM:
        if (header->stream_id == 0u) {
            return H2_PROTOCOL_ERROR;
        }
        return header->length == 4u ? H2_OK : H2_FRAME_SIZE_ERROR;
    case H2_FRAME_SETTINGS:
        if (header->stream_id != 0u) {
            return H2_PROTOCOL_ERROR;
        }
        if ((header->flags & H2_FLAG_ACK) != 0u) {
            return header->length == 0u ? H2_OK : H2_FRAME_SIZE_ERROR;
        }
        return header->length % 6u == 0u ? H2_OK : H2_FRAME_SIZE_ERROR;
    case H2_FRAME_PUSH_PROMISE:
        if (header->stream_id == 0u) {
            return H2_PROTOCOL_ERROR;
        }
        return header->length >= (((header->flags & H2_FLAG_PADDED) != 0u) ? 5u : 4u) ? H2_OK : H2_FRAME_SIZE_ERROR;
    case H2_FRAME_PING:
        if (header->stream_id != 0u) {
            return H2_PROTOCOL_ERROR;
        }
        return header->length == 8u ? H2_OK : H2_FRAME_SIZE_ERROR;
    case H2_FRAME_GOAWAY:
        if (header->stream_id != 0u) {
            return H2_PROTOCOL_ERROR;
        }
        return header->length >= 8u ? H2_OK : H2_FRAME_SIZE_ERROR;
    case H2_FRAME_WINDOW_UPDATE:
        return header->length == 4u ? H2_OK : H2_FRAME_SIZE_ERROR;
    case H2_FRAME_CONTINUATION:
        return h2_validate_stream_nonzero(header);
    default:
        return H2_OK;
    }
}

int h2_frame_parse_data(const h2_frame_header *header, const uint8_t *payload, h2_data_payload *out)
{
    size_t data_len;
    uint8_t pad_len;

    if (header == NULL || payload == NULL || out == NULL) {
        return H2_INTERNAL_ERROR;
    }
    if (header->type != H2_FRAME_DATA || h2_frame_validate_payload(header) != H2_OK) {
        return H2_PROTOCOL_ERROR;
    }

    data_len = header->length;
    pad_len = 0u;
    if ((header->flags & H2_FLAG_PADDED) != 0u) {
        if (data_len == 0u) {
            return H2_FRAME_SIZE_ERROR;
        }
        pad_len = payload[0];
        payload++;
        data_len--;
        if ((size_t)pad_len > data_len) {
            return H2_PROTOCOL_ERROR;
        }
        data_len -= pad_len;
    }

    out->data = payload;
    out->data_len = data_len;
    out->pad_len = pad_len;
    return H2_OK;
}

int h2_frame_parse_priority(const h2_frame_header *header, const uint8_t *payload, h2_priority_payload *out)
{
    if (header == NULL || payload == NULL || out == NULL) {
        return H2_INTERNAL_ERROR;
    }
    if (header->type != H2_FRAME_PRIORITY || h2_frame_validate_payload(header) != H2_OK) {
        return H2_FRAME_SIZE_ERROR;
    }

    out->exclusive = (payload[0] & 0x80u) != 0u;
    out->stream_dependency = h2_read_u31(payload);
    if (out->stream_dependency == header->stream_id) {
        return H2_PROTOCOL_ERROR;
    }
    out->weight = payload[4];
    return H2_OK;
}

int h2_frame_parse_headers(const h2_frame_header *header, const uint8_t *payload, h2_headers_payload *out)
{
    size_t block_len;
    size_t payload_pos;
    uint8_t pad_len;

    if (header == NULL || payload == NULL || out == NULL) {
        return H2_INTERNAL_ERROR;
    }
    if (header->type != H2_FRAME_HEADERS || h2_frame_validate_payload(header) != H2_OK) {
        return H2_PROTOCOL_ERROR;
    }

    block_len = header->length;
    payload_pos = 0u;
    pad_len = 0u;
    if ((header->flags & H2_FLAG_PADDED) != 0u) {
        if (block_len == 0u) {
            return H2_FRAME_SIZE_ERROR;
        }
        pad_len = payload[0];
        payload_pos++;
        block_len--;
        if ((size_t)pad_len > block_len) {
            return H2_PROTOCOL_ERROR;
        }
        block_len -= pad_len;
    }
    out->has_priority = (header->flags & H2_FLAG_PRIORITY) != 0u;
    if (out->has_priority) {
        h2_frame_header priority_header;

        if (block_len < 5u) {
            return H2_FRAME_SIZE_ERROR;
        }
        priority_header.length = 5u;
        priority_header.type = H2_FRAME_PRIORITY;
        priority_header.flags = 0u;
        priority_header.stream_id = header->stream_id;
        int ret;

        ret = h2_frame_parse_priority(&priority_header, payload + payload_pos, &out->priority);
        if (ret != H2_OK) {
            return ret;
        }
        payload_pos += 5u;
        block_len -= 5u;
    }

    out->header_block = payload + payload_pos;
    out->header_block_len = block_len;
    out->pad_len = pad_len;
    return H2_OK;
}

int h2_frame_parse_settings(const h2_frame_header *header, const uint8_t *payload, h2_setting *settings, size_t max_settings, size_t *settings_len)
{
    size_t item_count;
    size_t item_index;
    int valid;

    if (header == NULL || settings_len == NULL) {
        return H2_INTERNAL_ERROR;
    }
    if (header->type != H2_FRAME_SETTINGS) {
        return H2_PROTOCOL_ERROR;
    }
    valid = h2_frame_validate_payload(header);
    if (valid != H2_OK) {
        return valid;
    }
    if ((header->flags & H2_FLAG_ACK) != 0u) {
        *settings_len = 0u;
        return H2_OK;
    }
    if (header->length > 0u && payload == NULL) {
        return H2_INTERNAL_ERROR;
    }

    item_count = header->length / 6u;
    if (item_count > max_settings || (item_count > 0u && settings == NULL)) {
        return H2_INTERNAL_ERROR;
    }
    for (item_index = 0u; item_index < item_count; item_index++) {
        settings[item_index].id = h2_read_u16(payload + item_index * 6u);
        settings[item_index].value = h2_read_u32(payload + item_index * 6u + 2u);
    }
    *settings_len = item_count;
    return H2_OK;
}

int h2_frame_parse_window_update(const h2_frame_header *header, const uint8_t *payload, uint32_t *increment)
{
    int valid;

    if (header == NULL || payload == NULL || increment == NULL) {
        return H2_INTERNAL_ERROR;
    }
    if (header->type != H2_FRAME_WINDOW_UPDATE) {
        return H2_PROTOCOL_ERROR;
    }
    valid = h2_frame_validate_payload(header);
    if (valid != H2_OK) {
        return valid;
    }
    *increment = h2_read_u31(payload);
    return *increment == 0u ? H2_PROTOCOL_ERROR : H2_OK;
}

int h2_frame_parse_ping(const h2_frame_header *header, const uint8_t *payload, uint8_t opaque[8])
{
    int valid;

    if (header == NULL || payload == NULL || opaque == NULL) {
        return H2_INTERNAL_ERROR;
    }
    if (header->type != H2_FRAME_PING) {
        return H2_PROTOCOL_ERROR;
    }
    valid = h2_frame_validate_payload(header);
    if (valid != H2_OK) {
        return valid;
    }
    memcpy(opaque, payload, 8u);
    return H2_OK;
}

int h2_frame_parse_goaway(const h2_frame_header *header, const uint8_t *payload, h2_goaway_payload *out)
{
    int valid;

    if (header == NULL || payload == NULL || out == NULL) {
        return H2_INTERNAL_ERROR;
    }
    if (header->type != H2_FRAME_GOAWAY) {
        return H2_PROTOCOL_ERROR;
    }
    valid = h2_frame_validate_payload(header);
    if (valid != H2_OK) {
        return valid;
    }
    out->last_stream_id = h2_read_u31(payload);
    out->error_code = h2_read_u32(payload + 4u);
    out->debug_data = payload + 8u;
    out->debug_len = header->length - 8u;
    return H2_OK;
}

int h2_frame_parse_rst_stream(const h2_frame_header *header, const uint8_t *payload, uint32_t *error_code)
{
    int valid;

    if (header == NULL || payload == NULL || error_code == NULL) {
        return H2_INTERNAL_ERROR;
    }
    if (header->type != H2_FRAME_RST_STREAM) {
        return H2_PROTOCOL_ERROR;
    }
    valid = h2_frame_validate_payload(header);
    if (valid != H2_OK) {
        return valid;
    }
    *error_code = h2_read_u32(payload);
    return H2_OK;
}

int h2_frame_parse_continuation(const h2_frame_header *header, const uint8_t *payload, h2_continuation_payload *out)
{
    if (header == NULL || payload == NULL || out == NULL) {
        return H2_INTERNAL_ERROR;
    }
    if (header->type != H2_FRAME_CONTINUATION || h2_frame_validate_payload(header) != H2_OK) {
        return H2_PROTOCOL_ERROR;
    }
    out->header_block = payload;
    out->header_block_len = header->length;
    return H2_OK;
}

int h2_frame_parse_push_promise(const h2_frame_header *header, const uint8_t *payload, h2_push_promise_payload *out)
{
    size_t block_len;
    size_t payload_pos;
    uint8_t pad_len;

    if (header == NULL || payload == NULL || out == NULL) {
        return H2_INTERNAL_ERROR;
    }
    if (header->type != H2_FRAME_PUSH_PROMISE || h2_frame_validate_payload(header) != H2_OK) {
        return H2_PROTOCOL_ERROR;
    }

    block_len = header->length;
    payload_pos = 0u;
    pad_len = 0u;
    if ((header->flags & H2_FLAG_PADDED) != 0u) {
        pad_len = payload[0];
        payload_pos++;
        block_len--;
        if ((size_t)pad_len > block_len) {
            return H2_PROTOCOL_ERROR;
        }
        block_len -= pad_len;
    }
    if (block_len < 4u) {
        return H2_FRAME_SIZE_ERROR;
    }
    out->promised_stream_id = h2_read_u31(payload + payload_pos);
    out->header_block = payload + payload_pos + 4u;
    out->header_block_len = block_len - 4u;
    out->pad_len = pad_len;
    return H2_OK;
}

static size_t h2_frame_encode_raw(uint8_t *wire, size_t cap, uint8_t type, uint8_t flags, uint32_t stream_id, const uint8_t *payload, size_t payload_len)
{
    h2_frame_header header;

    if (wire == NULL || payload_len > H2_MAX_FRAME_SIZE || cap < H2_FRAME_HEADER_LEN + payload_len) {
        return 0u;
    }
    if (payload_len > 0u && payload == NULL) {
        return 0u;
    }
    header.length = (uint32_t)payload_len;
    header.type = type;
    header.flags = flags;
    header.stream_id = stream_id;
    if (h2_frame_write_header(wire, cap, &header) != H2_OK) {
        return 0u;
    }
    if (payload_len > 0u) {
        memcpy(wire + H2_FRAME_HEADER_LEN, payload, payload_len);
    }
    return H2_FRAME_HEADER_LEN + payload_len;
}

size_t h2_frame_encode_data(uint8_t *wire, size_t cap, uint32_t stream_id, uint8_t flags, const uint8_t *data, size_t data_len)
{
    if (stream_id == 0u || (flags & H2_FLAG_PADDED) != 0u) {
        return 0u;
    }
    return h2_frame_encode_raw(wire, cap, H2_FRAME_DATA, flags, stream_id, data, data_len);
}

size_t h2_frame_encode_headers(uint8_t *wire, size_t cap, uint32_t stream_id, uint8_t flags, const uint8_t *header_block, size_t header_block_len)
{
    if (stream_id == 0u || (flags & (H2_FLAG_PADDED | H2_FLAG_PRIORITY)) != 0u) {
        return 0u;
    }
    return h2_frame_encode_raw(wire, cap, H2_FRAME_HEADERS, flags, stream_id, header_block, header_block_len);
}

size_t h2_frame_encode_priority(uint8_t *wire, size_t cap, uint32_t stream_id, const h2_priority_payload *priority)
{
    uint8_t payload[5];
    uint32_t dependency;

    if (priority == NULL || stream_id == 0u) {
        return 0u;
    }
    dependency = priority->stream_dependency & 0x7fffffffu;
    if (priority->exclusive) {
        dependency |= 0x80000000u;
    }
    h2_write_u32(payload, dependency);
    payload[4] = priority->weight;
    return h2_frame_encode_raw(wire, cap, H2_FRAME_PRIORITY, 0u, stream_id, payload, sizeof(payload));
}

size_t h2_frame_encode_rst_stream(uint8_t *wire, size_t cap, uint32_t stream_id, uint32_t error_code)
{
    uint8_t payload[4];

    if (stream_id == 0u) {
        return 0u;
    }
    h2_write_u32(payload, error_code);
    return h2_frame_encode_raw(wire, cap, H2_FRAME_RST_STREAM, 0u, stream_id, payload, sizeof(payload));
}

size_t h2_frame_encode_settings(uint8_t *wire, size_t cap, uint8_t flags, const h2_setting *settings, size_t settings_len)
{
    uint8_t payload[96];
    size_t setting_index;

    if ((flags & H2_FLAG_ACK) != 0u) {
        return settings_len == 0u ? h2_frame_encode_raw(wire, cap, H2_FRAME_SETTINGS, H2_FLAG_ACK, 0u, NULL, 0u) : 0u;
    }
    if (settings_len > sizeof(payload) / 6u || (settings_len > 0u && settings == NULL)) {
        return 0u;
    }
    for (setting_index = 0u; setting_index < settings_len; setting_index++) {
        h2_write_u16(payload + setting_index * 6u, settings[setting_index].id);
        h2_write_u32(payload + setting_index * 6u + 2u, settings[setting_index].value);
    }
    return h2_frame_encode_raw(wire, cap, H2_FRAME_SETTINGS, flags, 0u, payload, settings_len * 6u);
}

size_t h2_frame_encode_push_promise(uint8_t *wire, size_t cap, uint32_t stream_id, uint8_t flags, uint32_t promised_stream_id, const uint8_t *header_block, size_t header_block_len)
{
    uint8_t payload[H2_DEFAULT_MAX_FRAME_SIZE];

    if (stream_id == 0u || promised_stream_id == 0u || (flags & H2_FLAG_PADDED) != 0u || header_block_len > SIZE_MAX - 4u || header_block_len > sizeof(payload) - 4u || (header_block_len > 0u && header_block == NULL)) {
        return 0u;
    }
    h2_write_u32(payload, promised_stream_id & 0x7fffffffu);
    if (header_block_len > 0u) {
        memcpy(payload + 4u, header_block, header_block_len);
    }
    return h2_frame_encode_raw(wire, cap, H2_FRAME_PUSH_PROMISE, flags, stream_id, payload, header_block_len + 4u);
}

size_t h2_frame_encode_ping(uint8_t *wire, size_t cap, uint8_t flags, const uint8_t opaque[8])
{
    if (opaque == NULL) {
        return 0u;
    }
    return h2_frame_encode_raw(wire, cap, H2_FRAME_PING, flags, 0u, opaque, 8u);
}

size_t h2_frame_encode_goaway(uint8_t *wire, size_t cap, uint32_t last_stream_id, uint32_t error_code, const uint8_t *debug_data, size_t debug_len)
{
    uint8_t payload[H2_DEFAULT_MAX_FRAME_SIZE];

    if (debug_len + 8u > sizeof(payload) || (debug_len > 0u && debug_data == NULL)) {
        return 0u;
    }
    h2_write_u32(payload, last_stream_id & 0x7fffffffu);
    h2_write_u32(payload + 4u, error_code);
    if (debug_len > 0u) {
        memcpy(payload + 8u, debug_data, debug_len);
    }
    return h2_frame_encode_raw(wire, cap, H2_FRAME_GOAWAY, 0u, 0u, payload, debug_len + 8u);
}

size_t h2_frame_encode_window_update(uint8_t *wire, size_t cap, uint32_t stream_id, uint32_t increment)
{
    uint8_t payload[4];

    if (increment == 0u || (increment & 0x80000000u) != 0u) {
        return 0u;
    }
    h2_write_u32(payload, increment);
    return h2_frame_encode_raw(wire, cap, H2_FRAME_WINDOW_UPDATE, 0u, stream_id, payload, sizeof(payload));
}

size_t h2_frame_encode_continuation(uint8_t *wire, size_t cap, uint32_t stream_id, uint8_t flags, const uint8_t *header_block, size_t header_block_len)
{
    if (stream_id == 0u) {
        return 0u;
    }
    return h2_frame_encode_raw(wire, cap, H2_FRAME_CONTINUATION, flags, stream_id, header_block, header_block_len);
}
