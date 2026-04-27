# HTTP/2 server implementation patterns from production C servers

Pinned sources cloned under `/tmp/http2-design-research`:

- h2o/h2o: `4aa96860e99cc2a2e2777433949bb05aed678ebe`
- nghttp2/nghttp2: `2a30faa0be35748211c78e6b6133f532e44c47d1`
- lpereira/lwan: `3da72b07f24c5bc201b2865901485cbd08b0715a`
- h2o/picohttpparser: `f4d94b48b31e0abae029ebeafcfd9ca0680ede58`

## 1. Per-stream state machine

### h2o: compact server-oriented stream lifecycle

Permalink: https://github.com/h2o/h2o/blob/4aa96860e99cc2a2e2777433949bb05aed678ebe/include/h2o/http2_internal.h#L35-L68

```c
typedef enum enum_h2o_http2_stream_state_t {
    /** stream in idle state (but registered; i.e. priority stream) */
    H2O_HTTP2_STREAM_STATE_IDLE,
    /** receiving headers */
    H2O_HTTP2_STREAM_STATE_RECV_HEADERS,
    /** receiving body (or trailers), waiting for the arrival of END_STREAM */
    H2O_HTTP2_STREAM_STATE_RECV_BODY,
    /** received request but haven't been assigned a handler */
    H2O_HTTP2_STREAM_STATE_REQ_PENDING,
    /** waiting for receiving response headers from the handler */
    H2O_HTTP2_STREAM_STATE_SEND_HEADERS,
    /** sending body */
    H2O_HTTP2_STREAM_STATE_SEND_BODY,
    /** received EOS from handler but still is sending body to client */
    H2O_HTTP2_STREAM_STATE_SEND_BODY_IS_FINAL,
    /** closed */
    H2O_HTTP2_STREAM_STATE_END_STREAM
} h2o_http2_stream_state_t;
```

Pattern: h2o maps RFC states into server work phases, not a literal RFC enum. For a server-only C11 implementation, this is better than representing every RFC state directly: it lets the scheduler ask “am I reading request body, pending app, or writing response?” without extra translation.

Adaptation: use one `enum http2_stream_state` with server phases (`IDLE`, `RECV_HEADERS`, `RECV_BODY`, `APP_PENDING`, `SEND_HEADERS`, `SEND_BODY`, `CLOSED`), plus small flags for `remote_closed` / `local_closed` if exact RFC reporting is needed.

Permalink: https://github.com/h2o/h2o/blob/4aa96860e99cc2a2e2777433949bb05aed678ebe/include/h2o/http2_internal.h#L347-L409

```c
inline void h2o_http2_stream_set_state(h2o_http2_conn_t *conn,
                                       h2o_http2_stream_t *stream,
                                       h2o_http2_stream_state_t new_state)
{
    switch (new_state) {
    case H2O_HTTP2_STREAM_STATE_RECV_HEADERS:
        assert(stream->state == H2O_HTTP2_STREAM_STATE_IDLE);
        if (h2o_http2_stream_is_push(stream->stream_id))
            h2o_http2_stream_update_open_slot(stream, &conn->num_streams.push);
        else
            h2o_http2_stream_update_open_slot(stream, &conn->num_streams.pull);
        stream->state = new_state;
        stream->req.timestamps.request_begin_at = h2o_gettimeofday(conn->super.ctx->loop);
        break;
    case H2O_HTTP2_STREAM_STATE_SEND_HEADERS:
        assert(stream->state == H2O_HTTP2_STREAM_STATE_REQ_PENDING);
        ++stream->_num_streams_slot->half_closed;
        stream->state = new_state;
        break;
    case H2O_HTTP2_STREAM_STATE_END_STREAM:
        /* decrements half_closed/send_body/open counters based on old state */
        stream->state = new_state;
        --stream->_num_streams_slot->open;
        stream->_num_streams_slot = NULL;
```

Pattern: state transition is centralized and updates accounting counters in the same place. The `assert()`s document legal transitions and catch protocol bugs in development.

Adaptation: make `stream_set_state(conn, stream, next)` the only writer to `stream->state`. Update `conn->open_streams`, `conn->half_closed_streams`, and scheduling membership inside it.

### h2o: stream map lifecycle

Permalink: https://github.com/h2o/h2o/blob/4aa96860e99cc2a2e2777433949bb05aed678ebe/lib/http2/connection.c#L260-L318

```c
void h2o_http2_conn_register_stream(h2o_http2_conn_t *conn,
                                    h2o_http2_stream_t *stream)
{
    khiter_t iter;
    int r;

    iter = kh_put(h2o_http2_stream_t, conn->streams, stream->stream_id, &r);
    assert(iter != kh_end(conn->streams));
    kh_val(conn->streams, iter) = stream;
}

void h2o_http2_conn_unregister_stream(h2o_http2_conn_t *conn,
                                      h2o_http2_stream_t *stream)
{
    h2o_http2_conn_preserve_stream_scheduler(conn, stream);

    khiter_t iter = kh_get(h2o_http2_stream_t, conn->streams, stream->stream_id);
    assert(iter != kh_end(conn->streams));
    kh_del(h2o_http2_stream_t, conn->streams, iter);
}
```

Pattern: stream IDs index a per-connection hash table; priority metadata is optionally preserved after close.

Adaptation: use a simple open-addressed hash map keyed by `uint32_t stream_id`. Since HTTP/2 stream IDs are sparse, do not use a dense array.

### nghttp2: RFC-visible state derived from state + shutdown flags

Permalink: https://github.com/nghttp2/nghttp2/blob/2a30faa0be35748211c78e6b6133f532e44c47d1/lib/nghttp2_stream.c#L140-L172

```c
nghttp2_stream_proto_state nghttp2_stream_get_state(nghttp2_stream *stream) {
  if (stream == &nghttp2_stream_root) {
    return NGHTTP2_STREAM_STATE_IDLE;
  }
  if (stream->flags & NGHTTP2_STREAM_FLAG_CLOSED) {
    return NGHTTP2_STREAM_STATE_CLOSED;
  }
  if (stream->flags & NGHTTP2_STREAM_FLAG_PUSH) {
    if (stream->shut_flags & NGHTTP2_SHUT_RD) {
      return NGHTTP2_STREAM_STATE_RESERVED_LOCAL;
    }
    if (stream->shut_flags & NGHTTP2_SHUT_WR) {
      return NGHTTP2_STREAM_STATE_RESERVED_REMOTE;
    }
  }
  if (stream->shut_flags & NGHTTP2_SHUT_RD) {
    return NGHTTP2_STREAM_STATE_HALF_CLOSED_REMOTE;
  }
  if (stream->shut_flags & NGHTTP2_SHUT_WR) {
    return NGHTTP2_STREAM_STATE_HALF_CLOSED_LOCAL;
  }
  return stream->state == NGHTTP2_STREAM_IDLE ? NGHTTP2_STREAM_STATE_IDLE
                                              : NGHTTP2_STREAM_STATE_OPEN;
}
```

Pattern: nghttp2 stores internal progress (`OPENING`, `OPENED`, `RESERVED`) and derives RFC states from `shut_flags`. This avoids explosion of transition cases.

Adaptation: store `state` + `shut_flags`. Expose a `stream_rfc_state()` helper for tests/debugging.

Permalink: https://github.com/nghttp2/nghttp2/blob/2a30faa0be35748211c78e6b6133f532e44c47d1/lib/nghttp2_session.c#L1216-L1268

```c
stream = nghttp2_session_get_stream_raw(session, stream_id);
if (stream) {
  assert(stream->state == NGHTTP2_STREAM_IDLE);
  assert(initial_state != NGHTTP2_STREAM_IDLE);
  --session->num_idle_streams;
} else {
  stream = nghttp2_mem_malloc(mem, sizeof(nghttp2_stream));
  stream_alloc = 1;
}

if (stream_alloc) {
  nghttp2_stream_init(stream, stream_id, flags, initial_state,
                      (int32_t)session->remote_settings.initial_window_size,
                      (int32_t)session->local_settings.initial_window_size,
                      stream_user_data);
  rv = nghttp2_map_insert(&session->streams, stream_id, stream);
}

if (initial_state == NGHTTP2_STREAM_RESERVED) {
  if (nghttp2_session_is_my_stream_id(session, stream_id))
    nghttp2_stream_shutdown(stream, NGHTTP2_SHUT_RD);   /* reserved local */
  else
    nghttp2_stream_shutdown(stream, NGHTTP2_SHUT_WR);   /* reserved remote */
}
```

Pattern: stream allocation initializes both local and remote windows, then registers in a map exactly once.

Adaptation: allocate stream only when HEADERS/PUSH_PROMISE creates it; initialize flow-control windows from current settings, not constants.

## 2. Flow-control accounting

### h2o: receive-side connection and stream WINDOW_UPDATE thresholds

Permalink: https://github.com/h2o/h2o/blob/4aa96860e99cc2a2e2777433949bb05aed678ebe/lib/http2/connection.c#L804-L818

```c
static void send_window_update(h2o_http2_conn_t *conn, uint32_t stream_id,
                               h2o_http2_window_t *window, size_t delta)
{
    assert(delta <= INT32_MAX);
    h2o_http2_encode_window_update_frame(&conn->_write.buf, stream_id, (int32_t)delta);
    h2o_http2_conn_request_write(conn);
    h2o_http2_window_update(window, delta);
}

void update_stream_input_window(h2o_http2_conn_t *conn, h2o_http2_stream_t *stream, size_t delta)
{
    stream->input_window.bytes_unnotified += delta;
    if (stream->input_window.bytes_unnotified >= h2o_http2_window_get_avail(&stream->input_window.window)) {
        send_window_update(conn, stream->stream_id, &stream->input_window.window,
                           stream->input_window.bytes_unnotified);
        stream->input_window.bytes_unnotified = 0;
    }
}
```

Pattern: stream WINDOW_UPDATE is emitted when consumed-but-unnotified bytes reach current available window.

Adaptation: track `bytes_unnotified` per stream. When application consumes body bytes, emit WINDOW_UPDATE and increase local window.

Permalink: https://github.com/h2o/h2o/blob/4aa96860e99cc2a2e2777433949bb05aed678ebe/lib/http2/connection.c#L981-L988

```c
/* update connection-level window */
h2o_http2_window_consume_window(&conn->_input_window, frame->length);
if (h2o_http2_window_get_avail(&conn->_input_window) <=
    H2O_HTTP2_SETTINGS_HOST_CONNECTION_WINDOW_SIZE / 2)
    send_window_update(conn, 0, &conn->_input_window,
        H2O_HTTP2_SETTINGS_HOST_CONNECTION_WINDOW_SIZE -
        h2o_http2_window_get_avail(&conn->_input_window));
```

Pattern: connection window is refreshed at half capacity with stream ID 0.

Adaptation: simple and robust: if `conn_recv_window <= initial_conn_window / 2`, send `WINDOW_UPDATE(0, initial - current)`.

### h2o: send-side connection + stream window cap

Permalink: https://github.com/h2o/h2o/blob/4aa96860e99cc2a2e2777433949bb05aed678ebe/lib/http2/stream.c#L107-L129

```c
static size_t calc_max_payload_size(h2o_http2_conn_t *conn,
                                    h2o_http2_stream_t *stream)
{
    ssize_t conn_max, stream_max;

    if ((conn_max = h2o_http2_conn_get_buffer_window(conn)) <= 0)
        return 0;
    if ((stream_max = h2o_http2_window_get_avail(&stream->output_window)) <= 0)
        return 0;
    return sz_min(sz_min(conn_max, stream_max), conn->peer_settings.max_frame_size);
}

static void commit_data_header(..., size_t length, h2o_send_state_t send_state)
{
    h2o_http2_encode_frame_header(..., length, H2O_HTTP2_FRAME_TYPE_DATA, ...);
    h2o_http2_window_consume_window(&conn->_write.window, length);
    h2o_http2_window_consume_window(&stream->output_window, length);
}
```

Pattern: outgoing DATA length is min(connection window, stream window, max frame size), then both windows are decremented atomically when frame is committed.

Adaptation: one `http2_data_budget(conn, stream)` helper; never write DATA larger than that budget.

### nghttp2: threshold function + automatic WINDOW_UPDATE

Permalink: https://github.com/nghttp2/nghttp2/blob/2a30faa0be35748211c78e6b6133f532e44c47d1/lib/nghttp2_helper.c#L248-L251

```c
int nghttp2_should_send_window_update(int32_t local_window_size,
                                      int32_t recv_window_size) {
  return recv_window_size > 0 && recv_window_size >= local_window_size / 2;
}
```

Permalink: https://github.com/nghttp2/nghttp2/blob/2a30faa0be35748211c78e6b6133f532e44c47d1/lib/nghttp2_session.c#L5084-L5137

```c
rv = adjust_recv_window_size(&stream->recv_window_size, delta_size,
                             stream->local_window_size);
if (send_window_update &&
    !(session->opt_flags & NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE) &&
    stream->window_update_queued == 0 &&
    nghttp2_should_send_window_update(stream->local_window_size,
                                      stream->recv_window_size)) {
  rv = nghttp2_session_add_window_update(
    session, NGHTTP2_FLAG_NONE, stream->stream_id, stream->recv_window_size);
  stream->recv_window_size = 0;
}

rv = adjust_recv_window_size(&session->recv_window_size, delta_size,
                             session->local_window_size);
if (session->window_update_queued == 0 &&
    nghttp2_should_send_window_update(session->local_window_size,
                                      session->recv_window_size)) {
  rv = nghttp2_session_add_window_update(session, NGHTTP2_FLAG_NONE, 0,
                                         session->recv_window_size);
  session->recv_window_size = 0;
}
```

Pattern: same threshold function handles stream and connection windows; stream ID 0 is connection-level.

Adaptation: implement one `should_send_window_update(local, received_since_update)` predicate and call it for both scopes.

Permalink: https://github.com/nghttp2/nghttp2/blob/2a30faa0be35748211c78e6b6133f532e44c47d1/lib/nghttp2_session.c#L4740-L4790

```c
if (NGHTTP2_MAX_WINDOW_SIZE - frame->window_update.window_size_increment <
    session->remote_window_size) {
  return session_handle_invalid_connection(session, frame,
                                           NGHTTP2_ERR_FLOW_CONTROL, NULL);
}
session->remote_window_size += frame->window_update.window_size_increment;

if (NGHTTP2_MAX_WINDOW_SIZE - frame->window_update.window_size_increment <
    stream->remote_window_size) {
  return session_handle_invalid_connection(session, frame,
                                           NGHTTP2_ERR_FLOW_CONTROL,
                                           "WINDOW_UPDATE: window size overflow");
}
stream->remote_window_size += frame->window_update.window_size_increment;

if (stream->remote_window_size > 0 &&
    nghttp2_stream_check_deferred_by_flow_control(stream))
  rv = session_resume_deferred_stream_item(session, stream,
      NGHTTP2_STREAM_FLAG_DEFERRED_FLOW_CONTROL);
```

Pattern: inbound WINDOW_UPDATE overflow checks happen before addition; flow-control-blocked stream is resumed only when window becomes positive.

Adaptation: always check `MAX_WINDOW - increment < window` before adding; keep a `DEFERRED_FLOW_CONTROL` bit in streams with queued DATA.

## 3. Frame parsing and generation

### h2o: 9-byte frame header parser/writer

Permalink: https://github.com/h2o/h2o/blob/4aa96860e99cc2a2e2777433949bb05aed678ebe/lib/http2/frame.c#L68-L85

```c
uint8_t *h2o_http2_encode_frame_header(uint8_t *dst, size_t length,
                                       uint8_t type, uint8_t flags,
                                       int32_t stream_id)
{
    if (length > 0xffffff)
        h2o_fatal("invalid length");
    dst = h2o_http2_encode24u(dst, (uint32_t)length);
    *dst++ = type;
    *dst++ = flags;
    dst = h2o_http2_encode32u(dst, stream_id);
    return dst;
}

static uint8_t *allocate_frame(h2o_buffer_t **buf, size_t length,
                               uint8_t type, uint8_t flags, int32_t stream_id)
{
    h2o_iovec_t alloced = h2o_buffer_reserve(buf, H2O_HTTP2_FRAME_HEADER_SIZE + length);
    (*buf)->size += H2O_HTTP2_FRAME_HEADER_SIZE + length;
    return h2o_http2_encode_frame_header((uint8_t *)alloced.base, length, type, flags, stream_id);
}
```

Permalink: https://github.com/h2o/h2o/blob/4aa96860e99cc2a2e2777433949bb05aed678ebe/lib/http2/frame.c#L130-L149

```c
ssize_t h2o_http2_decode_frame(h2o_http2_frame_t *frame, const uint8_t *src,
                               size_t len, size_t max_frame_size,
                               const char **err_desc)
{
    if (len < H2O_HTTP2_FRAME_HEADER_SIZE)
        return H2O_HTTP2_ERROR_INCOMPLETE;
    frame->length = h2o_http2_decode24u(src);
    frame->type = src[3];
    frame->flags = src[4];
    frame->stream_id = h2o_http2_decode32u(src + 5) & 0x7fffffff;
    if (frame->length > max_frame_size)
        return H2O_HTTP2_ERROR_FRAME_SIZE;
    if (len < H2O_HTTP2_FRAME_HEADER_SIZE + frame->length)
        return H2O_HTTP2_ERROR_INCOMPLETE;
    frame->payload = src + H2O_HTTP2_FRAME_HEADER_SIZE;
    return H2O_HTTP2_FRAME_HEADER_SIZE + frame->length;
}
```

Pattern: parse returns consumed byte count or an error/incomplete sentinel. The caller can run it in a loop over the socket buffer.

Adaptation: use exactly this interface: `ssize_t frame_decode(frame*, buf, len, max_frame)`, returning `INCOMPLETE` until the whole frame is present.

### h2o: payload dispatcher table

Permalink: https://github.com/h2o/h2o/blob/4aa96860e99cc2a2e2777433949bb05aed678ebe/lib/http2/connection.c#L1299-L1327

```c
ssize_t expect_default(h2o_http2_conn_t *conn, const uint8_t *src,
                       size_t len, const char **err_desc)
{
    h2o_http2_frame_t frame;
    static int (*FRAME_HANDLERS[])(h2o_http2_conn_t *, h2o_http2_frame_t *, const char **) = {
        handle_data_frame,                /* DATA */
        handle_headers_frame,             /* HEADERS */
        handle_priority_frame,            /* PRIORITY */
        handle_rst_stream_frame,          /* RST_STREAM */
        handle_settings_frame,            /* SETTINGS */
        handle_push_promise_frame,        /* PUSH_PROMISE */
        handle_ping_frame,                /* PING */
        handle_goaway_frame,              /* GOAWAY */
        handle_window_update_frame,       /* WINDOW_UPDATE */
        handle_invalid_continuation_frame /* CONTINUATION */
    };
    ssize_t ret = h2o_http2_decode_frame(&frame, src, len,
        H2O_HTTP2_SETTINGS_HOST_MAX_FRAME_SIZE, err_desc);
    if (ret < 0) return ret;
    if (frame.type < sizeof(FRAME_HANDLERS) / sizeof(FRAME_HANDLERS[0]))
        ret = FRAME_HANDLERS[frame.type](conn, &frame, err_desc) ?: ret;
    return ret;
}
```

Pattern: frame type becomes an array index. Unknown frames are ignored, as required by HTTP/2.

Adaptation: use a handler table for frame types 0-9. Special-case CONTINUATION by swapping the read expectation while decoding header blocks.

### nghttp2: minimal 9-byte frame header functions

Permalink: https://github.com/nghttp2/nghttp2/blob/2a30faa0be35748211c78e6b6133f532e44c47d1/lib/nghttp2_frame.c#L37-L52

```c
void nghttp2_frame_pack_frame_hd(uint8_t *buf, const nghttp2_frame_hd *hd) {
  nghttp2_put_uint32be(&buf[0], (uint32_t)(hd->length << 8));
  buf[3] = hd->type;
  buf[4] = hd->flags;
  nghttp2_put_uint32be(&buf[5], (uint32_t)hd->stream_id);
  /* ignore hd->reserved for now */
}

void nghttp2_frame_unpack_frame_hd(nghttp2_frame_hd *hd, const uint8_t *buf) {
  *hd = (nghttp2_frame_hd){
    .length = nghttp2_get_uint32(&buf[0]) >> 8,
    .stream_id = nghttp2_get_uint32(&buf[5]) & NGHTTP2_STREAM_ID_MASK,
    .type = buf[3],
    .flags = buf[4],
  };
}
```

Pattern: 24-bit length is encoded by writing a big-endian 32-bit value shifted left 8. Very compact.

Adaptation: copy this for `http2_pack_frame_header()` / `http2_unpack_frame_header()`.

## 4. Connection preface handling

### h2o: server validates exact 24-byte preface, then sends SETTINGS

Permalink: https://github.com/h2o/h2o/blob/4aa96860e99cc2a2e2777433949bb05aed678ebe/lib/http2/connection.c#L33-L72

```c
static const h2o_iovec_t CONNECTION_PREFACE = {H2O_STRLIT("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n")};

static void enqueue_server_preface(h2o_http2_conn_t *conn)
{
    h2o_http2_settings_kvpair_t settings[] = {
        {H2O_HTTP2_SETTINGS_HEADER_TABLE_SIZE, H2O_HTTP2_SETTINGS_HOST_HEADER_TABLE_SIZE},
        {H2O_HTTP2_SETTINGS_ENABLE_PUSH, H2O_HTTP2_SETTINGS_HOST_ENABLE_PUSH},
        {H2O_HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, conn->super.ctx->globalconf->http2.max_streams},
        {H2O_HTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL, 1}};
    h2o_http2_encode_settings_frame(&conn->_write.buf, settings, sizeof(settings) / sizeof(settings[0]));
    h2o_http2_encode_window_update_frame(&conn->_write.buf, 0,
        H2O_HTTP2_SETTINGS_HOST_CONNECTION_WINDOW_SIZE -
        H2O_HTTP2_SETTINGS_HOST_STREAM_INITIAL_WINDOW_SIZE);
}
```

Permalink: https://github.com/h2o/h2o/blob/4aa96860e99cc2a2e2777433949bb05aed678ebe/lib/http2/connection.c#L1330-L1352

```c
static ssize_t expect_preface(h2o_http2_conn_t *conn, const uint8_t *src,
                              size_t len, const char **err_desc)
{
    if (len < CONNECTION_PREFACE.len)
        return H2O_HTTP2_ERROR_INCOMPLETE;
    if (memcmp(src, CONNECTION_PREFACE.base, CONNECTION_PREFACE.len) != 0)
        return H2O_HTTP2_ERROR_PROTOCOL_CLOSE_IMMEDIATELY;

    enqueue_server_preface(conn);
    h2o_http2_conn_request_write(conn);
    conn->_read_expect = expect_default;
    return CONNECTION_PREFACE.len;
}
```

Pattern: read state is a function pointer (`_read_expect`). Before preface it points at `expect_preface`; after exact match it points at frame parser.

Adaptation: use `conn->read_state = READ_PREFACE` or `conn->read_expect = expect_preface`; after consuming 24 bytes, queue SETTINGS + connection WINDOW_UPDATE, then parse frames.

### nghttp2: incremental preface comparison

Permalink: https://github.com/nghttp2/nghttp2/blob/2a30faa0be35748211c78e6b6133f532e44c47d1/lib/includes/nghttp2/nghttp2.h#L246-L259

```c
#define NGHTTP2_CLIENT_MAGIC "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define NGHTTP2_CLIENT_MAGIC_LEN 24
```

Permalink: https://github.com/nghttp2/nghttp2/blob/2a30faa0be35748211c78e6b6133f532e44c47d1/lib/nghttp2_session.c#L5458-L5473

```c
case NGHTTP2_IB_READ_CLIENT_MAGIC:
  readlen = nghttp2_min_size(inlen, iframe->payloadleft);
  if (memcmp(&NGHTTP2_CLIENT_MAGIC[NGHTTP2_CLIENT_MAGIC_LEN -
                                   iframe->payloadleft],
             in, readlen) != 0) {
    return NGHTTP2_ERR_BAD_CLIENT_MAGIC;
  }
  iframe->payloadleft -= readlen;
  in += readlen;
  if (iframe->payloadleft == 0) {
    session_inbound_frame_reset(session);
    iframe->state = NGHTTP2_IB_READ_FIRST_SETTINGS;
  }
  break;
```

Pattern: supports partial TCP reads without buffering the entire preface first by comparing only the available prefix.

Adaptation: store `preface_left` or `preface_seen`; compare chunks against `CLIENT_MAGIC + preface_seen`.

## 5. HPACK encode/decode

### nghttp2: header block decode loop

Permalink: https://github.com/nghttp2/nghttp2/blob/2a30faa0be35748211c78e6b6133f532e44c47d1/lib/nghttp2_session.c#L3542-L3583

```c
DEBUGF("recv: decoding header block %zu bytes\n", inlen);
for (;;) {
  inflate_flags = 0;
  proclen = nghttp2_hd_inflate_hd_nv(&session->hd_inflater, &nv,
                                     &inflate_flags, in, inlen, final);
  if (nghttp2_is_fatal((int)proclen)) {
    return (int)proclen;
  }
  if (proclen < 0) {
    rv = nghttp2_session_terminate_session(session, NGHTTP2_COMPRESSION_ERROR);
    return NGHTTP2_ERR_HEADER_COMP;
  }
  in += proclen;
  inlen -= (size_t)proclen;
  *readlen_ptr += (size_t)proclen;

  if (call_header_cb && (inflate_flags & NGHTTP2_HD_INFLATE_EMIT)) {
    if (subject_stream && session_enforce_http_messaging(session))
      rv = nghttp2_http_on_header(session, subject_stream, frame, &nv, trailer);
```

Pattern: HPACK inflater is resumable; it emits one header at a time with `NGHTTP2_HD_INFLATE_EMIT`.

Adaptation: for curl GET support, implement enough HPACK to decode indexed headers, literal indexed names, and literals without indexing; feed emitted pseudo-headers into request validation.

### nghttp2: HPACK opcode dispatch

Permalink: https://github.com/nghttp2/nghttp2/blob/2a30faa0be35748211c78e6b6133f532e44c47d1/lib/nghttp2_hd.c#L1939-L1981

```c
for (; in != last || busy;) {
  busy = 0;
  switch (inflater->state) {
  case NGHTTP2_HD_STATE_EXPECT_TABLE_SIZE:
    if ((*in & 0xe0u) != 0x20u) {
      rv = NGHTTP2_ERR_HEADER_COMP;
      goto fail;
    }
  case NGHTTP2_HD_STATE_INFLATE_START:
  case NGHTTP2_HD_STATE_OPCODE:
    if ((*in & 0xe0u) == 0x20u) {
      inflater->opcode = NGHTTP2_HD_OPCODE_INDEXED;
      inflater->state = NGHTTP2_HD_STATE_READ_TABLE_SIZE;
    } else if (*in & 0x80u) {
      inflater->opcode = NGHTTP2_HD_OPCODE_INDEXED;
      inflater->state = NGHTTP2_HD_STATE_READ_INDEX;
    } else {
      if (*in == 0x40u || *in == 0 || *in == 0x10u) {
        inflater->opcode = NGHTTP2_HD_OPCODE_NEWNAME;
        inflater->state = NGHTTP2_HD_STATE_NEWNAME_CHECK_NAMELEN;
      } else {
        inflater->opcode = NGHTTP2_HD_OPCODE_INDNAME;
        inflater->state = NGHTTP2_HD_STATE_READ_INDEX;
      }
```

Pattern: HPACK is bytecode. Top bits select representation: table size update, indexed header, literal new name, literal indexed name.

Adaptation: build a tiny state machine around this dispatch. Static table is mandatory; dynamic table can initially be capped to 0 via SETTINGS_HEADER_TABLE_SIZE=0 to reduce implementation scope.

### nghttp2: HPACK encoder searches static/dynamic table first

Permalink: https://github.com/nghttp2/nghttp2/blob/2a30faa0be35748211c78e6b6133f532e44c47d1/lib/nghttp2_hd.c#L1373-L1418

```c
static int deflate_nv(nghttp2_hd_deflater *deflater, nghttp2_bufs *bufs,
                      const nghttp2_nv *nv) {
  search_result res;
  nghttp2_ssize idx;
  int32_t token;

  token = lookup_token(nv->name, nv->namelen);
  res = search_hd_table(&deflater->ctx, nv, token, indexing_mode,
                        &deflater->map, hash);
  idx = res.index;

  if (res.name_value_match) {
    rv = emit_indexed_block(bufs, (size_t)idx);
    return rv;
  }
  if (indexing_mode == NGHTTP2_HD_WITH_INDEXING) {
    /* add to dynamic table, then emit literal */
```

Pattern: exact static-table matches are emitted as one indexed byte/varint; unmatched headers become literals.

Adaptation for minimal 200 OK: encode `:status: 200` as static table index 8, then literals without indexing for `content-length` / `content-type` if needed. This is enough for curl.

### lwan: standalone HPACK Huffman decoder, but no complete HTTP/2 server

Permalink: https://github.com/lpereira/lwan/blob/3da72b07f24c5bc201b2865901485cbd08b0715a/src/lib/lwan-h2-huffman.c#L312-L381

```c
struct lwan_h2_huffman_decoder {
    struct bit_reader bit_reader;
    struct uint8_ring_buffer buffer;
};

void lwan_h2_huffman_init(struct lwan_h2_huffman_decoder *huff,
                          const uint8_t *input, size_t input_len)
{
    huff->bit_reader = (struct bit_reader){
        .bitptr = input,
        .total_bitcount = (int64_t)input_len * 8,
    };
    uint8_ring_buffer_init(&huff->buffer);
}

ssize_t lwan_h2_huffman_next(struct lwan_h2_huffman_decoder *huff)
{
    struct bit_reader *reader = &huff->bit_reader;
    while (reader->total_bitcount > 7) {
        uint8_t peeked_byte = peek_byte(reader);
        if (LIKELY(level0[peeked_byte].num_bits)) {
            uint8_ring_buffer_try_put_copy(buffer, level0[peeked_byte].symbol);
            consume(reader, level0[peeked_byte].num_bits);
            continue;
        }
```

Pattern: lwan has an optimized HPACK Huffman decoder table, but repository search showed no full HTTP/2 frame/state implementation. picohttpparser similarly has no HTTP/2 code; it is HTTP/1 parser only.

Adaptation: do not depend on Huffman initially. Send `SETTINGS_HEADER_TABLE_SIZE=0` and accept non-Huffman literals first, then borrow lwan-style table decoder later for compressed request headers.

## 6. Event-loop integration pattern

### h2o: socket callback drains input buffer then emits pending writes

Permalink: https://github.com/h2o/h2o/blob/4aa96860e99cc2a2e2777433949bb05aed678ebe/lib/http2/connection.c#L1355-L1411

```c
static int parse_input(h2o_http2_conn_t *conn)
{
    while (conn->state < H2O_HTTP2_CONN_STATE_IS_CLOSING &&
           conn->sock->input->size != 0) {
        const char *err_desc = NULL;
        ssize_t ret = conn->_read_expect(conn,
            (uint8_t *)conn->sock->input->bytes, conn->sock->input->size,
            &err_desc);
        if (ret == H2O_HTTP2_ERROR_INCOMPLETE) {
            break;
        } else if (ret < 0) {
            enqueue_goaway(conn, (int)ret, ...);
            return close_connection(conn);
        }
        h2o_buffer_consume(&conn->sock->input, ret);
    }
    return 0;
}

static void on_read(h2o_socket_t *sock, const char *err)
{
    h2o_http2_conn_t *conn = sock->data;
    if (parse_input(conn) != 0) return;
    if (h2o_timer_is_linked(&conn->_write.timeout_entry))
        do_emit_writereq(conn);
}
```

Pattern: read callback does not parse just one frame; it drains all complete frames currently buffered. Writes are scheduled after parsing, reducing syscall churn.

Adaptation: with kqueue/io_uring, on readable: append bytes to `conn->in`, loop `read_expect`, consume complete frames, then flush output if queued and no write in flight.

Permalink: https://github.com/h2o/h2o/blob/4aa96860e99cc2a2e2777433949bb05aed678ebe/lib/http2/connection.c#L1590-L1606

```c
void do_emit_writereq(h2o_http2_conn_t *conn)
{
    assert(conn->_write.buf_in_flight == NULL);
    if (conn->state < H2O_HTTP2_CONN_STATE_IS_CLOSING &&
        h2o_http2_conn_get_buffer_window(conn) > 0)
        h2o_http2_scheduler_run(&conn->scheduler, emit_writereq_of_openref, conn);

    if (conn->_write.buf->size != 0) {
        h2o_iovec_t buf = {conn->_write.buf->bytes, conn->_write.buf->size};
        h2o_socket_write(conn->sock, &buf, 1, on_write_complete);
        conn->_write.buf_in_flight = conn->_write.buf;
        h2o_buffer_init(&conn->_write.buf, &h2o_http2_wbuf_buffer_prototype);
    }
}
```

Pattern: one output buffer in flight, one buffer accumulating new writes. This avoids modifying memory owned by the kernel write operation.

Adaptation: use `out_pending` and `out_inflight`; when write starts, swap pending to inflight and allocate/reset pending.

### lwan: coroutine + epoll event interest update

Permalink: https://github.com/lpereira/lwan/blob/3da72b07f24c5bc201b2865901485cbd08b0715a/src/lib/lwan-thread.c#L571-L622

```c
static int update_epoll_flags(const struct lwan *lwan,
                              struct lwan_connection *conn,
                              int epoll_fd,
                              enum lwan_connection_coro_yield yield_result)
{
    static const enum lwan_connection_flags or_mask[CONN_CORO_MAX] = {
        [CONN_CORO_WANT_READ_WRITE] = CONN_EVENTS_READ_WRITE,
        [CONN_CORO_WANT_READ] = CONN_EVENTS_READ,
        [CONN_CORO_WANT_WRITE] = CONN_EVENTS_WRITE,
        [CONN_CORO_SUSPEND] = CONN_SUSPENDED,
        [CONN_CORO_RESUME] = CONN_EVENTS_WRITE,
    };
    conn->flags |= or_mask[yield_result];
    conn->flags &= and_mask[yield_result];
    if (LWAN_EVENTS(conn->flags) == LWAN_EVENTS(prev_flags))
        return 0;
    struct epoll_event event = {.events = conn_flags_to_epoll_events(conn->flags),
                                .data.ptr = conn};
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
}
```

Permalink: https://github.com/lpereira/lwan/blob/3da72b07f24c5bc201b2865901485cbd08b0715a/src/lib/lwan-thread.c#L1226-L1327

```c
static void *thread_io_loop(void *data)
{
    for (;;) {
        int timeout = turn_timer_wheel(&tq, t, epoll_fd);
        int n_fds = epoll_wait(epoll_fd, events, max_events, timeout);
        for (struct epoll_event *event = events; n_fds--; event++) {
            struct lwan_connection *conn = event->data.ptr;
            if (conn->flags & CONN_LISTENER) {
                accept_waiting_clients(t, conn, &switcher, &tq);
                continue;
            }
            if (UNLIKELY(event->events & (EPOLLRDHUP | EPOLLHUP))) {
                timeout_queue_expire(&tq, conn);
                continue;
            }
            if (!conn->coro)
                spawn_coro(conn, &switcher, &tq);
            resume_coro(&tq, conn, conn, epoll_fd);
        }
    }
}
```

Pattern: application code can yield `WANT_READ` / `WANT_WRITE`; central loop translates that into epoll flags. Lwan has HPACK Huffman code but no full HTTP/2 implementation in this snapshot.

Adaptation: if using a coroutine-free C11 event loop, keep the same idea as an explicit connection state: parser returns `WANT_READ`, writer returns `WANT_WRITE`, event backend updates interest set.

## Recommended minimal implementation plan for this project

1. Copy h2o’s read-expect pattern: `expect_preface`, `expect_default`, `expect_continuation` function pointers.
2. Copy nghttp2/h2o frame header helpers exactly: 24-bit length, type, flags, 31-bit stream ID.
3. Use h2o-style server stream phases plus nghttp2-style `shut_flags` for RFC state tests.
4. Implement flow control with three integers per stream: `local_window`, `remote_window`, `recv_window_size`; two at connection level too.
5. Emit WINDOW_UPDATE at half-window threshold using nghttp2’s predicate.
6. Start HPACK minimal: static table indexed decoding + literal indexed-name/no-index decoding; encode `:status: 200` indexed, other response fields literals without indexing.
7. Defer full dynamic table/Huffman until curl GET + DATA + SETTINGS/WINDOW_UPDATE are stable.
