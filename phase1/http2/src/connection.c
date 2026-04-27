#include "http2/connection.h"

#include "http2/hpack.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static void h2_connection_compact_output(h2_connection *conn)
{
    if (conn->output_sent == 0u) {
        return;
    }
    if (conn->output_sent >= conn->output_len) {
        conn->output_len = 0u;
        conn->output_sent = 0u;
        return;
    }
    memmove(conn->output, conn->output + conn->output_sent, conn->output_len - conn->output_sent);
    conn->output_len -= conn->output_sent;
    conn->output_sent = 0u;
}

static int h2_connection_queue_bytes(h2_connection *conn, const uint8_t *data, size_t data_len)
{
    h2_connection_compact_output(conn);
    if (data_len > H2_CONN_OUTPUT_CAP - conn->output_len || (data_len > 0u && data == NULL)) {
        return H2_INTERNAL_ERROR;
    }
    if (data_len > 0u) {
        memcpy(conn->output + conn->output_len, data, data_len);
        conn->output_len += data_len;
    }
    return H2_OK;
}

static int h2_connection_queue_goaway(h2_connection *conn, uint32_t error_code)
{
    uint8_t wire[H2_FRAME_HEADER_LEN + 8u];
    size_t wire_len;

    if (conn->goaway_sent) {
        return H2_OK;
    }
    wire_len = h2_frame_encode_goaway(wire, sizeof(wire), conn->last_stream_id, error_code, NULL, 0u);
    if (wire_len == 0u) {
        return H2_INTERNAL_ERROR;
    }
    conn->goaway_sent = true;
    conn->goaway_error = error_code;
    return h2_connection_queue_bytes(conn, wire, wire_len);
}

static int h2_connection_queue_settings(h2_connection *conn)
{
    h2_setting settings[1];
    uint8_t wire[H2_FRAME_HEADER_LEN + 6u];
    size_t wire_len;

    settings[0].id = H2_SETTINGS_ENABLE_PUSH;
    settings[0].value = 0u;
    wire_len = h2_frame_encode_settings(wire, sizeof(wire), 0u, settings, 1u);
    if (wire_len == 0u) {
        return H2_INTERNAL_ERROR;
    }
    return h2_connection_queue_bytes(conn, wire, wire_len);
}

static int h2_connection_queue_settings_ack(h2_connection *conn)
{
    uint8_t wire[H2_FRAME_HEADER_LEN];
    size_t wire_len;

    wire_len = h2_frame_encode_settings(wire, sizeof(wire), H2_FLAG_ACK, NULL, 0u);
    if (wire_len == 0u) {
        return H2_INTERNAL_ERROR;
    }
    return h2_connection_queue_bytes(conn, wire, wire_len);
}

static int h2_connection_queue_window_update(h2_connection *conn, uint32_t stream_id, uint32_t increment)
{
    uint8_t wire[H2_FRAME_HEADER_LEN + 4u];
    size_t wire_len;

    wire_len = h2_frame_encode_window_update(wire, sizeof(wire), stream_id, increment);
    if (wire_len == 0u) {
        return H2_INTERNAL_ERROR;
    }
    return h2_connection_queue_bytes(conn, wire, wire_len);
}

static int h2_connection_queue_rst_stream(h2_connection *conn, uint32_t stream_id, uint32_t error_code)
{
    uint8_t wire[H2_FRAME_HEADER_LEN + 4u];
    size_t wire_len;

    wire_len = h2_frame_encode_rst_stream(wire, sizeof(wire), stream_id, error_code);
    if (wire_len == 0u) {
        return H2_INTERNAL_ERROR;
    }
    return h2_connection_queue_bytes(conn, wire, wire_len);
}

static h2_stream *h2_connection_find_stream(h2_connection *conn, uint32_t stream_id)
{
    size_t pos;

    for (pos = 0u; pos < H2_CONN_MAX_STREAMS; pos++) {
        if (conn->streams[pos].id == stream_id) {
            return &conn->streams[pos];
        }
    }
    return NULL;
}

static h2_stream *h2_connection_get_stream(h2_connection *conn, uint32_t stream_id)
{
    h2_stream *stream;
    size_t pos;

    stream = h2_connection_find_stream(conn, stream_id);
    if (stream != NULL) {
        return stream;
    }
    for (pos = 0u; pos < H2_CONN_MAX_STREAMS; pos++) {
        if (conn->streams[pos].id == 0u) {
            h2_stream_init(&conn->streams[pos], stream_id, conn->initial_stream_window);
            return &conn->streams[pos];
        }
    }
    return NULL;
}

static int h2_connection_refresh_recv_windows(h2_connection *conn, h2_stream *stream)
{
    if (conn->conn_recv_window <= H2_DEFAULT_WINDOW_SIZE / 2) {
        uint32_t increment;

        increment = (uint32_t)(H2_DEFAULT_WINDOW_SIZE - conn->conn_recv_window);
        conn->conn_recv_window += (int32_t)increment;
        if (h2_connection_queue_window_update(conn, 0u, increment) != H2_OK) {
            return H2_INTERNAL_ERROR;
        }
    }
    if (stream != NULL && stream->recv_window <= H2_DEFAULT_WINDOW_SIZE / 2) {
        uint32_t increment;

        increment = (uint32_t)(H2_DEFAULT_WINDOW_SIZE - stream->recv_window);
        stream->recv_window += (int32_t)increment;
        if (h2_connection_queue_window_update(conn, stream->id, increment) != H2_OK) {
            return H2_INTERNAL_ERROR;
        }
    }
    return H2_OK;
}

static int h2_connection_apply_settings(h2_connection *conn, const h2_frame_header *header, const uint8_t *payload)
{
    h2_setting settings[32];
    size_t settings_len;
    size_t pos;
    int ret;

    ret = h2_frame_parse_settings(header, payload, settings, sizeof(settings) / sizeof(settings[0]), &settings_len);
    if (ret != H2_OK) {
        return ret;
    }
    if ((header->flags & H2_FLAG_ACK) != 0u) {
        return H2_OK;
    }
    for (pos = 0u; pos < settings_len; pos++) {
        if (settings[pos].id == H2_SETTINGS_ENABLE_PUSH) {
            if (settings[pos].value > 1u) {
                return H2_PROTOCOL_ERROR;
            }
        } else if (settings[pos].id == H2_SETTINGS_INITIAL_WINDOW_SIZE) {
            int64_t delta;
            size_t stream_pos;

            if (settings[pos].value > INT32_MAX) {
                return H2_FLOW_CONTROL_ERROR;
            }
            delta = (int64_t)settings[pos].value - (int64_t)conn->initial_stream_window;
            for (stream_pos = 0u; stream_pos < H2_CONN_MAX_STREAMS; stream_pos++) {
                if (conn->streams[stream_pos].id != 0u) {
                    int64_t new_window;

                    new_window = (int64_t)conn->streams[stream_pos].send_window + delta;
                    if (new_window < INT32_MIN || new_window > INT32_MAX) {
                        return H2_FLOW_CONTROL_ERROR;
                    }
                    conn->streams[stream_pos].send_window = (int32_t)new_window;
                }
            }
            conn->initial_stream_window = (int32_t)settings[pos].value;
        } else if (settings[pos].id == H2_SETTINGS_MAX_FRAME_SIZE) {
            if (settings[pos].value < H2_DEFAULT_MAX_FRAME_SIZE || settings[pos].value > H2_MAX_FRAME_SIZE) {
                return H2_PROTOCOL_ERROR;
            }
            conn->max_frame_size = settings[pos].value;
        }
    }
    return h2_connection_queue_settings_ack(conn);
}

static int h2_connection_queue_deferred_data(h2_connection *conn, h2_stream *stream, const char *body, size_t body_len)
{
    uint8_t wire[H2_FRAME_HEADER_LEN + 1024u];
    size_t wire_len;

    if ((int64_t)conn->conn_send_window < (int64_t)body_len || (int64_t)stream->send_window < (int64_t)body_len) {
        stream->response_body_deferred = true;
        return H2_OK;
    }
    wire_len = h2_frame_encode_data(wire, sizeof(wire), stream->id, H2_FLAG_END_STREAM, (const uint8_t *)body, body_len);
    if (wire_len == 0u || h2_connection_queue_bytes(conn, wire, wire_len) != H2_OK) {
        return H2_INTERNAL_ERROR;
    }
    conn->conn_send_window -= (int32_t)body_len;
    stream->send_window -= (int32_t)body_len;
    stream->response_body_deferred = false;
    stream->response_sent = true;
    conn->responses_sent++;
    return h2_stream_send_end_stream(stream);
}

static int h2_connection_queue_response(h2_connection *conn, h2_stream *stream)
{
    uint8_t header_block[512];
    uint8_t wire[H2_FRAME_HEADER_LEN + 1024u];
    char content_length[32];
    char body[320];
    size_t body_len;
    size_t pos;
    size_t chunk_len;
    size_t wire_len;
    int printed;

    if (stream->response_sent) {
        return H2_OK;
    }
    if (stream->path[0] == '\0') {
        if (sizeof(stream->path) < 2u) {
            return H2_INTERNAL_ERROR;
        }
        stream->path[0] = '/';
        stream->path[1] = '\0';
    }
    printed = snprintf(body, sizeof(body), "%s\n", stream->path);
    if (printed < 0 || (size_t)printed >= sizeof(body)) {
        return H2_INTERNAL_ERROR;
    }
    body_len = (size_t)printed;
    printed = snprintf(content_length, sizeof(content_length), "%zu", body_len);
    if (printed < 0 || (size_t)printed >= sizeof(content_length)) {
        return H2_INTERNAL_ERROR;
    }

    if (!stream->response_headers_sent) {
        pos = 0u;
        chunk_len = h2_hpack_encode_indexed(header_block + pos, sizeof(header_block) - pos, 8u);
        if (chunk_len == 0u) {
            return H2_INTERNAL_ERROR;
        }
        pos += chunk_len;
        chunk_len = h2_hpack_encode_literal_new_name(header_block + pos, sizeof(header_block) - pos, "content-type", "text/plain");
        if (chunk_len == 0u) {
            return H2_INTERNAL_ERROR;
        }
        pos += chunk_len;
        chunk_len = h2_hpack_encode_literal_new_name(header_block + pos, sizeof(header_block) - pos, "content-length", content_length);
        if (chunk_len == 0u) {
            return H2_INTERNAL_ERROR;
        }
        pos += chunk_len;
        chunk_len = h2_hpack_encode_literal_new_name(header_block + pos, sizeof(header_block) - pos, "server", "c11-h2");
        if (chunk_len == 0u) {
            return H2_INTERNAL_ERROR;
        }
        pos += chunk_len;

        wire_len = h2_frame_encode_headers(wire, sizeof(wire), stream->id, H2_FLAG_END_HEADERS, header_block, pos);
        if (wire_len == 0u || h2_connection_queue_bytes(conn, wire, wire_len) != H2_OK) {
            return H2_INTERNAL_ERROR;
        }
        stream->response_headers_sent = true;
    }
    return h2_connection_queue_deferred_data(conn, stream, body, body_len);
}

static int h2_connection_flush_deferred_responses(h2_connection *conn)
{
    size_t pos;

    for (pos = 0u; pos < H2_CONN_MAX_STREAMS; pos++) {
        if (conn->streams[pos].id != 0u && conn->streams[pos].response_body_deferred) {
            int ret;

            ret = h2_connection_queue_response(conn, &conn->streams[pos]);
            if (ret != H2_OK) {
                return ret;
            }
        }
    }
    return H2_OK;
}

static int h2_connection_finish_headers(h2_connection *conn, uint32_t stream_id, uint8_t flags, const uint8_t *header_block, size_t header_block_len)
{
    h2_stream *stream;
    int ret;

    if ((stream_id & 1u) == 0u) {
        return H2_PROTOCOL_ERROR;
    }
    if (stream_id <= conn->last_stream_id) {
        return H2_PROTOCOL_ERROR;
    }
    stream = h2_connection_get_stream(conn, stream_id);
    if (stream == NULL) {
        return H2_REFUSED_STREAM;
    }
    conn->last_stream_id = stream_id;
    ret = h2_hpack_extract_path(header_block, header_block_len, stream->path, sizeof(stream->path));
    if (ret != H2_OK) {
        return ret;
    }
    ret = h2_stream_receive_headers(stream, (flags & H2_FLAG_END_STREAM) != 0u);
    if (ret != H2_OK) {
        return ret;
    }
    return h2_connection_queue_response(conn, stream);
}

static int h2_connection_append_header_block(h2_connection *conn, const uint8_t *data, size_t data_len)
{
    if (data_len > sizeof(conn->header_block) - conn->header_block_len) {
        return H2_COMPRESSION_ERROR;
    }
    if (data_len > 0u) {
        memcpy(conn->header_block + conn->header_block_len, data, data_len);
        conn->header_block_len += data_len;
    }
    return H2_OK;
}

static int h2_connection_process_headers(h2_connection *conn, const h2_frame_header *header, const uint8_t *payload)
{
    h2_headers_payload parsed;
    int ret;

    ret = h2_frame_parse_headers(header, payload, &parsed);
    if (ret != H2_OK) {
        return ret;
    }
    if ((header->flags & H2_FLAG_END_HEADERS) == 0u) {
        if (conn->header_block_open) {
            return H2_PROTOCOL_ERROR;
        }
        conn->header_block_open = true;
        conn->header_block_stream_id = header->stream_id;
        conn->header_block_flags = header->flags;
        conn->header_block_len = 0u;
        return h2_connection_append_header_block(conn, parsed.header_block, parsed.header_block_len);
    }
    return h2_connection_finish_headers(conn, header->stream_id, header->flags, parsed.header_block, parsed.header_block_len);
}

static int h2_connection_process_continuation(h2_connection *conn, const h2_frame_header *header, const uint8_t *payload)
{
    h2_continuation_payload parsed;
    int ret;

    if (!conn->header_block_open || header->stream_id != conn->header_block_stream_id) {
        return H2_PROTOCOL_ERROR;
    }
    ret = h2_frame_parse_continuation(header, payload, &parsed);
    if (ret != H2_OK) {
        return ret;
    }
    ret = h2_connection_append_header_block(conn, parsed.header_block, parsed.header_block_len);
    if (ret != H2_OK) {
        return ret;
    }
    if ((header->flags & H2_FLAG_END_HEADERS) == 0u) {
        return H2_OK;
    }

    conn->header_block_open = false;
    return h2_connection_finish_headers(conn, conn->header_block_stream_id, conn->header_block_flags, conn->header_block, conn->header_block_len);
}

static int h2_connection_process_data(h2_connection *conn, const h2_frame_header *header, const uint8_t *payload)
{
    h2_data_payload parsed;
    h2_stream *stream;
    int ret;

    ret = h2_frame_parse_data(header, payload, &parsed);
    if (ret != H2_OK) {
        return ret;
    }
    stream = h2_connection_get_stream(conn, header->stream_id);
    if (stream == NULL) {
        return H2_REFUSED_STREAM;
    }
    if ((size_t)conn->conn_recv_window < header->length) {
        return H2_FLOW_CONTROL_ERROR;
    }
    conn->conn_recv_window -= (int32_t)header->length;
    ret = h2_stream_receive_data(stream, header->length, (header->flags & H2_FLAG_END_STREAM) != 0u);
    if (ret != H2_OK) {
        return ret;
    }
    (void)parsed;
    return h2_connection_refresh_recv_windows(conn, stream->id == 0u ? NULL : stream);
}

static int h2_connection_process_window_update(h2_connection *conn, const h2_frame_header *header, const uint8_t *payload)
{
    uint32_t increment;
    int ret;

    ret = h2_frame_parse_window_update(header, payload, &increment);
    if (ret != H2_OK) {
        return ret;
    }
    if (header->stream_id == 0u) {
        if ((int64_t)conn->conn_send_window + (int64_t)increment > (int64_t)INT32_MAX) {
            return H2_FLOW_CONTROL_ERROR;
        }
        conn->conn_send_window += (int32_t)increment;
        return h2_connection_flush_deferred_responses(conn);
    }
    {
        h2_stream *stream;

        stream = h2_connection_find_stream(conn, header->stream_id);
        if (stream == NULL) {
            return H2_OK;
        }
        ret = h2_stream_add_send_window(stream, increment);
        if (ret != H2_OK) {
            return ret;
        }
    }
    return h2_connection_flush_deferred_responses(conn);
}

static bool h2_connection_is_stream_error(int error_code)
{
    return error_code == H2_REFUSED_STREAM || error_code == H2_STREAM_CLOSED;
}

static int h2_connection_process_frame(h2_connection *conn, const h2_frame_header *header, const uint8_t *payload)
{
    int ret;

    if (header->length > conn->max_frame_size) {
        return H2_FRAME_SIZE_ERROR;
    }
    if (conn->header_block_open && (header->type != H2_FRAME_CONTINUATION || header->stream_id != conn->header_block_stream_id)) {
        return H2_PROTOCOL_ERROR;
    }

    switch (header->type) {
    case H2_FRAME_DATA:
        return h2_connection_process_data(conn, header, payload);
    case H2_FRAME_HEADERS:
        return h2_connection_process_headers(conn, header, payload);
    case H2_FRAME_PRIORITY:
    {
        h2_priority_payload priority;

        ret = h2_frame_parse_priority(header, payload, &priority);
        if (ret != H2_OK) {
            return ret;
        }
        (void)priority;
        return H2_OK;
    }
    case H2_FRAME_RST_STREAM: {
        h2_stream *stream;
        uint32_t error_code;

        ret = h2_frame_parse_rst_stream(header, payload, &error_code);
        if (ret != H2_OK) {
            return ret;
        }
        stream = h2_connection_find_stream(conn, header->stream_id);
        if (stream != NULL) {
            h2_stream_close(stream);
        }
        (void)error_code;
        return H2_OK;
    }
    case H2_FRAME_SETTINGS:
        return h2_connection_apply_settings(conn, header, payload);
    case H2_FRAME_PUSH_PROMISE:
        return H2_PROTOCOL_ERROR;
    case H2_FRAME_PING: {
        uint8_t opaque[8];
        uint8_t wire[H2_FRAME_HEADER_LEN + 8u];
        size_t wire_len;

        ret = h2_frame_parse_ping(header, payload, opaque);
        if (ret != H2_OK || (header->flags & H2_FLAG_ACK) != 0u) {
            return ret;
        }
        wire_len = h2_frame_encode_ping(wire, sizeof(wire), H2_FLAG_ACK, opaque);
        if (wire_len == 0u) {
            return H2_INTERNAL_ERROR;
        }
        return h2_connection_queue_bytes(conn, wire, wire_len);
    }
    case H2_FRAME_GOAWAY:
        return h2_frame_validate_payload(header);
    case H2_FRAME_WINDOW_UPDATE:
        return h2_connection_process_window_update(conn, header, payload);
    case H2_FRAME_CONTINUATION:
        return h2_connection_process_continuation(conn, header, payload);
    default:
        return H2_OK;
    }
}

void h2_connection_init(h2_connection *conn)
{
    memset(conn, 0, sizeof(*conn));
    conn->max_frame_size = H2_DEFAULT_MAX_FRAME_SIZE;
    conn->initial_stream_window = H2_DEFAULT_WINDOW_SIZE;
    conn->conn_send_window = H2_DEFAULT_WINDOW_SIZE;
    conn->conn_recv_window = H2_DEFAULT_WINDOW_SIZE;
}

int h2_connection_feed(h2_connection *conn, const uint8_t *data, size_t data_len)
{
    if (conn == NULL || (data == NULL && data_len > 0u)) {
        return H2_INTERNAL_ERROR;
    }
    if (data_len > H2_CONN_INPUT_CAP - conn->input_len) {
        return H2_INTERNAL_ERROR;
    }
    if (data_len > 0u) {
        memcpy(conn->input + conn->input_len, data, data_len);
        conn->input_len += data_len;
    }
    if (!conn->preface_seen) {
        if (conn->input_len < H2_CLIENT_PREFACE_LEN) {
            return H2_OK;
        }
        if (memcmp(conn->input, H2_CLIENT_PREFACE, H2_CLIENT_PREFACE_LEN) != 0) {
            (void)h2_connection_queue_goaway(conn, H2_PROTOCOL_ERROR);
            return H2_PROTOCOL_ERROR;
        }
        memmove(conn->input, conn->input + H2_CLIENT_PREFACE_LEN, conn->input_len - H2_CLIENT_PREFACE_LEN);
        conn->input_len -= H2_CLIENT_PREFACE_LEN;
        conn->preface_seen = true;
        if (!conn->settings_sent) {
            int ret;

            ret = h2_connection_queue_settings(conn);
            if (ret != H2_OK) {
                return ret;
            }
            conn->settings_sent = true;
        }
    }

    while (conn->input_len >= H2_FRAME_HEADER_LEN) {
        h2_frame_header header;
        size_t whole_len;
        int ret;

        ret = h2_frame_parse_header(conn->input, conn->input_len, &header);
        if (ret != H2_OK) {
            return ret;
        }
        whole_len = H2_FRAME_HEADER_LEN + (size_t)header.length;
        if (whole_len > conn->input_len) {
            break;
        }
        ret = h2_connection_process_frame(conn, &header, conn->input + H2_FRAME_HEADER_LEN);
        if (ret != H2_OK) {
            if (h2_connection_is_stream_error(ret) && header.stream_id != 0u) {
                int rst_ret;

                rst_ret = h2_connection_queue_rst_stream(conn, header.stream_id, (uint32_t)ret);
                if (rst_ret != H2_OK) {
                    return rst_ret;
                }
            } else {
                (void)h2_connection_queue_goaway(conn, (uint32_t)ret);
                return ret;
            }
        }
        memmove(conn->input, conn->input + whole_len, conn->input_len - whole_len);
        conn->input_len -= whole_len;
    }
    return H2_OK;
}

bool h2_connection_wants_write(const h2_connection *conn)
{
    return conn != NULL && conn->output_sent < conn->output_len;
}

const uint8_t *h2_connection_output(const h2_connection *conn)
{
    if (conn == NULL || conn->output_sent >= conn->output_len) {
        return NULL;
    }
    return conn->output + conn->output_sent;
}

size_t h2_connection_output_len(const h2_connection *conn)
{
    if (conn == NULL || conn->output_sent >= conn->output_len) {
        return 0u;
    }
    return conn->output_len - conn->output_sent;
}

void h2_connection_consume_output(h2_connection *conn, size_t sent_len)
{
    size_t available;

    if (conn == NULL) {
        return;
    }
    available = h2_connection_output_len(conn);
    if (sent_len > available) {
        sent_len = available;
    }
    conn->output_sent += sent_len;
    h2_connection_compact_output(conn);
}
