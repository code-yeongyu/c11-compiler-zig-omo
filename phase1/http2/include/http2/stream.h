#ifndef HTTP2_STREAM_H
#define HTTP2_STREAM_H

#include "http2/frame.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define H2_STREAM_PATH_CAP 256u

typedef enum h2_stream_state {
    H2_STREAM_STATE_IDLE,
    H2_STREAM_STATE_OPEN,
    H2_STREAM_STATE_HALF_CLOSED_REMOTE,
    H2_STREAM_STATE_HALF_CLOSED_LOCAL,
    H2_STREAM_STATE_CLOSED
} h2_stream_state;

typedef struct h2_stream {
    uint32_t id;
    h2_stream_state state;
    int32_t send_window;
    int32_t recv_window;
    bool response_headers_sent;
    bool response_body_deferred;
    bool response_sent;
    char path[H2_STREAM_PATH_CAP];
} h2_stream;

void h2_stream_init(h2_stream *stream, uint32_t stream_id, int32_t initial_window);
void h2_stream_close(h2_stream *stream);
int h2_stream_receive_headers(h2_stream *stream, bool end_stream);
int h2_stream_receive_data(h2_stream *stream, size_t data_len, bool end_stream);
int h2_stream_send_end_stream(h2_stream *stream);
int h2_stream_add_send_window(h2_stream *stream, uint32_t increment);

#endif
