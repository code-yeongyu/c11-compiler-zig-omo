#include "http2/stream.h"

#include <limits.h>
#include <string.h>

void h2_stream_init(h2_stream *stream, uint32_t stream_id, int32_t initial_window)
{
    memset(stream, 0, sizeof(*stream));
    stream->id = stream_id;
    stream->state = H2_STREAM_STATE_IDLE;
    stream->send_window = initial_window;
    stream->recv_window = H2_DEFAULT_WINDOW_SIZE;
}

int h2_stream_receive_headers(h2_stream *stream, bool end_stream)
{
    if (stream == NULL) {
        return H2_INTERNAL_ERROR;
    }
    if (stream->state != H2_STREAM_STATE_IDLE) {
        return H2_PROTOCOL_ERROR;
    }
    stream->state = end_stream ? H2_STREAM_STATE_HALF_CLOSED_REMOTE : H2_STREAM_STATE_OPEN;
    return H2_OK;
}

int h2_stream_receive_data(h2_stream *stream, size_t data_len, bool end_stream)
{
    if (stream == NULL || data_len > (size_t)INT32_MAX) {
        return H2_INTERNAL_ERROR;
    }
    if (stream->state != H2_STREAM_STATE_OPEN && stream->state != H2_STREAM_STATE_HALF_CLOSED_LOCAL) {
        return H2_STREAM_CLOSED;
    }
    if (stream->recv_window < (int32_t)data_len) {
        return H2_FLOW_CONTROL_ERROR;
    }
    stream->recv_window -= (int32_t)data_len;
    if (end_stream) {
        stream->state = stream->state == H2_STREAM_STATE_HALF_CLOSED_LOCAL ? H2_STREAM_STATE_CLOSED : H2_STREAM_STATE_HALF_CLOSED_REMOTE;
    }
    return H2_OK;
}

int h2_stream_send_end_stream(h2_stream *stream)
{
    if (stream == NULL) {
        return H2_INTERNAL_ERROR;
    }
    if (stream->state == H2_STREAM_STATE_OPEN) {
        stream->state = H2_STREAM_STATE_HALF_CLOSED_LOCAL;
        return H2_OK;
    }
    if (stream->state == H2_STREAM_STATE_HALF_CLOSED_REMOTE) {
        stream->state = H2_STREAM_STATE_CLOSED;
        return H2_OK;
    }
    return H2_STREAM_CLOSED;
}

int h2_stream_add_send_window(h2_stream *stream, uint32_t increment)
{
    if (stream == NULL || increment == 0u) {
        return H2_PROTOCOL_ERROR;
    }
    if (increment > (uint32_t)(INT32_MAX - stream->send_window)) {
        return H2_FLOW_CONTROL_ERROR;
    }
    stream->send_window += (int32_t)increment;
    return H2_OK;
}
