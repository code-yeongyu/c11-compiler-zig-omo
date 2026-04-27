#ifndef HTTP2_CONNECTION_H
#define HTTP2_CONNECTION_H

#include "http2/stream.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define H2_CLIENT_PREFACE "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define H2_CLIENT_PREFACE_LEN 24u
#define H2_CONN_INPUT_CAP 131072u
#define H2_CONN_OUTPUT_CAP 131072u
#define H2_CONN_MAX_STREAMS 128u

typedef struct h2_connection {
    bool preface_seen;
    bool settings_sent;
    bool goaway_sent;
    uint32_t goaway_error;
    uint32_t last_stream_id;
    uint32_t max_frame_size;
    int32_t initial_stream_window;
    int32_t conn_send_window;
    int32_t conn_recv_window;
    uint32_t responses_sent;
    bool header_block_open;
    uint32_t header_block_stream_id;
    uint8_t header_block_flags;
    uint8_t header_block[65536];
    size_t header_block_len;
    h2_stream streams[H2_CONN_MAX_STREAMS];
    uint8_t input[H2_CONN_INPUT_CAP];
    size_t input_len;
    uint8_t output[H2_CONN_OUTPUT_CAP];
    size_t output_len;
    size_t output_sent;
} h2_connection;

void h2_connection_init(h2_connection *conn);
int h2_connection_feed(h2_connection *conn, const uint8_t *data, size_t data_len);
bool h2_connection_wants_write(const h2_connection *conn);
const uint8_t *h2_connection_output(const h2_connection *conn);
size_t h2_connection_output_len(const h2_connection *conn);
void h2_connection_consume_output(h2_connection *conn, size_t sent_len);

#endif
