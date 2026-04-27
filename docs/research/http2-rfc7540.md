# HTTP/2 RFC 7540 / HPACK RFC 7541 reference

Authoritative sources: RFC 7540, RFC 7541. RFC 7540 is obsoleted by RFC 9113, but this project targets RFC 7540 interop semantics.

## 1. Common HTTP/2 frame header

All HTTP/2 frames start with a fixed 9-octet header followed by `Length` octets of payload. All numeric fields are unsigned and network byte order. `Length` excludes the 9-byte header. RFC 7540 §2.2, §4.1.

```text
+-----------------------------------------------+
|                 Length (24)                   |  bytes 0..2
+---------------+---------------+---------------+
|   Type (8)    |   Flags (8)   |                  bytes 3..4
+-+-------------+---------------+-------------------------------+
|R|                 Stream Identifier (31)                      |  bytes 5..8
+=+=============================================================+
|                   Frame Payload (0...)                      ...
+---------------------------------------------------------------+
```

Header byte layout:

```text
0: length[23:16]
1: length[15:8]
2: length[7:0]
3: type
4: flags
5: R bit in bit 7, stream_id bits 30..24 in bits 6..0
6: stream_id bits 23..16
7: stream_id bits 15..8
8: stream_id bits 7..0
```

Rules:

- `Length` default max is 16,384. Values > 16,384 MUST NOT be sent unless peer set larger `SETTINGS_MAX_FRAME_SIZE`. Legal advertised range: 16,384..16,777,215. RFC 7540 §4.1, §4.2, §6.5.2.
- Receiver MUST be capable of receiving/minimally processing 16,384-byte payload + 9-byte header. RFC 7540 §4.2.
- Frame exceeding connection max, type-specific max, or too small for mandatory payload: send `FRAME_SIZE_ERROR`. If frame can alter whole-connection state, treat as connection error. This includes header-block frames, SETTINGS, and any stream 0 frame. RFC 7540 §4.2.
- Unknown `Type`: implementation MUST ignore and discard. RFC 7540 §4.1, §5.5.
- Unknown/undefined flags for a known type: receiver MUST ignore; sender MUST leave unset. RFC 7540 §4.1.
- Reserved `R` bit: sender MUST set 0; receiver MUST ignore. RFC 7540 §4.1.
- Stream identifier `0` is reserved for connection-level frames; per-frame sections define valid stream IDs. RFC 7540 §4.1.
- Header blocks are HPACK-compressed; HEADERS/PUSH_PROMISE/CONTINUATION fragments MUST be contiguous, no interleaved frame of any type or stream until END_HEADERS. Receiver MUST decompress even discarded header blocks or connection error `COMPRESSION_ERROR`. RFC 7540 §4.3.

## 2. Minimum frame types

Type registry: RFC 7540 §11.2. Frame definitions: RFC 7540 §6.

| Type | ID | Section | Purpose | Minimal server action |
|---|---:|---|---|---|
| DATA | `0x0` | §6.1 | Request/response body bytes | Must parse, flow-control count, handle END_STREAM/PADDED |
| HEADERS | `0x1` | §6.2 | Opens stream and carries HPACK fragment | Must parse and HPACK-decode request headers |
| PRIORITY | `0x2` | §6.3 | Stream priority signal | Must parse if received; can ignore priority scheduling |
| RST_STREAM | `0x3` | §6.4 | Immediate stream termination | Must close/reset stream state |
| SETTINGS | `0x4` | §6.5 | Connection parameters | Must parse, apply known settings, ACK |
| PING | `0x6` | §6.7 | RTT/keepalive | Must echo opaque data with ACK |
| GOAWAY | `0x7` | §6.8 | Connection shutdown | Must stop creating/processing newer streams |
| WINDOW_UPDATE | `0x8` | §6.9 | Flow-control credit | Must update windows |
| CONTINUATION | `0x9` | §6.10 | Continues header block | Must parse after HEADERS without END_HEADERS |

Frame types not listed here but defined by RFC 7540: PUSH_PROMISE `0x5` (§6.6). A server that does not implement server push can send `SETTINGS_ENABLE_PUSH=0`; receiving PUSH_PROMISE on a server connection is already invalid direction for normal clients. Unknown extension frames MUST be ignored/discarded. RFC 7540 §5.5.

### 2.1 DATA (`type = 0x0`) — RFC 7540 §6.1

Frame header:

```text
length[3] type=0x0 flags stream_id!=0 payload[length]
```

Payload:

```text
Without PADDED:
+-------------------------------+
|          Data (*)             |
+-------------------------------+

With PADDED flag:
+---------------+---------------+
| Pad Length(8) | Data (*)    ...|
+---------------+---------------+
| Padding (*)                   |
+-------------------------------+
```

Defined flags:

- `END_STREAM = 0x1`: this frame is last frame sender will send on this stream; transitions stream toward half-closed/closed. RFC 7540 §6.1, §5.1.
- `PADDED = 0x8`: payload starts with 1-byte Pad Length, followed by data and padding. RFC 7540 §6.1.

Rules:

- DATA frames MUST be associated with a stream. If `stream_id == 0`: connection error `PROTOCOL_ERROR`. RFC 7540 §6.1.
- DATA frames are subject to flow control and count toward stream and connection windows. Pad Length and padding are included in flow-control accounting. RFC 7540 §6.1, §5.2.
- DATA can be sent only when stream is `open` or `half-closed (remote)` from receiver perspective. If received for stream not in those states, respond with stream error `STREAM_CLOSED`. RFC 7540 §6.1.
- Padding bytes MAY be any value; receiver ignores. If padding length >= payload length: connection error `PROTOCOL_ERROR`. RFC 7540 §6.1.
- Undefined flags: ignore. Unknown frame types: ignore/discard. RFC 7540 §4.1.

### 2.2 HEADERS (`type = 0x1`) — RFC 7540 §6.2

Frame header:

```text
length[3] type=0x1 flags stream_id!=0 payload[length]
```

Payload variants:

```text
No PADDED, no PRIORITY:
+-------------------------------+
| Header Block Fragment (*)     |
+-------------------------------+

PADDED only:
+---------------+---------------+
| Pad Length(8) | Header Block Fragment (*) ...
+---------------+---------------+
| Padding (*)                   |
+-------------------------------+

PRIORITY only:
+-+-----------------------------+
|E| Stream Dependency (31)      |  4 bytes
+-+-----------------------------+
| Weight (8)                    |  1 byte, encoded 0..255 for weight 1..256
+-------------------------------+
| Header Block Fragment (*)     |
+-------------------------------+

PADDED + PRIORITY:
+---------------+-+-------------+
| Pad Length(8) |E| Stream Dependency (31) ...
+---------------+-+-------------+
| Weight (8)                    |
+-------------------------------+
| Header Block Fragment (*)     |
+-------------------------------+
| Padding (*)                   |
+-------------------------------+
```

Defined flags:

- `END_STREAM = 0x1`: header block is last sender data for stream. RFC 7540 §6.2.
- `END_HEADERS = 0x4`: header block ends in this frame. If clear, one or more CONTINUATION frames for same stream MUST follow immediately. RFC 7540 §6.2, §4.3.
- `PADDED = 0x8`: payload starts with Pad Length. RFC 7540 §6.2.
- `PRIORITY = 0x20`: exclusive bit + stream dependency + weight present. RFC 7540 §6.2, §5.3.

Rules:

- HEADERS MUST be associated with a stream. If `stream_id == 0`: connection error `PROTOCOL_ERROR`. RFC 7540 §6.2.
- HEADERS opens an idle stream. RFC 7540 §5.1, §6.2.
- HEADERS can also be sent on open or half-closed(remote) streams for trailers. RFC 7540 §8.1.
- If PRIORITY is present, payload needs at least 5 bytes after optional Pad Length; too small: `FRAME_SIZE_ERROR`. RFC 7540 §4.2, §6.2.
- If padding length >= remaining payload length after priority fields: connection error `PROTOCOL_ERROR`. RFC 7540 §6.2.
- A HEADERS without END_HEADERS starts a header-block sequence. Next frame MUST be CONTINUATION on same stream; otherwise connection error `PROTOCOL_ERROR`. RFC 7540 §4.3, §6.10.
- HPACK decoding errors: connection error `COMPRESSION_ERROR`. RFC 7540 §4.3.

### 2.3 PRIORITY (`type = 0x2`) — RFC 7540 §6.3

Frame header:

```text
length=5 type=0x2 flags=0 stream_id!=0 payload[5]
```

Payload:

```text
+-+-------------------------------------------------------------+
|E|                  Stream Dependency (31)                     |  4 bytes
+-+-------------+-----------------------------------------------+
|   Weight (8)  |                                                  1 byte
+---------------+
```

Fields:

- `E`: exclusive dependency bit. RFC 7540 §5.3.1, §6.3.
- `Stream Dependency`: stream this stream depends on; `0` means root. RFC 7540 §5.3.1, §6.3.
- `Weight`: encoded as unsigned 8-bit value; add 1 to get actual weight 1..256. RFC 7540 §5.3.2, §6.3.

Defined flags: none. Undefined flags MUST be ignored. RFC 7540 §4.1, §6.3.

Rules:

- PRIORITY MUST be associated with a stream. If `stream_id == 0`: connection error `PROTOCOL_ERROR`. RFC 7540 §6.3.
- Length MUST be exactly 5. Otherwise frame size error. RFC 7540 §6.3.
- PRIORITY can be sent in any stream state, including idle or closed. RFC 7540 §5.1, §6.3.
- Stream dependency on itself: stream error `PROTOCOL_ERROR`. RFC 7540 §5.3.1.
- Implementation MAY ignore priority scheduling while still parsing frames. RFC 7540 §5.3.

### 2.4 RST_STREAM (`type = 0x3`) — RFC 7540 §6.4

Frame header:

```text
length=4 type=0x3 flags=0 stream_id!=0 payload[4]
```

Payload:

```text
+---------------------------------------------------------------+
|                        Error Code (32)                         |
+---------------------------------------------------------------+
```

Defined flags: none. Undefined flags MUST be ignored. RFC 7540 §4.1, §6.4.

Rules:

- RST_STREAM allows immediate stream termination. RFC 7540 §6.4.
- RST_STREAM MUST be associated with a stream. If `stream_id == 0`: connection error `PROTOCOL_ERROR`. RFC 7540 §6.4.
- Length MUST be exactly 4. Otherwise connection error `FRAME_SIZE_ERROR`. RFC 7540 §6.4.
- After sending RST_STREAM, sender MUST NOT send additional frames for that stream except PRIORITY. Receiver of RST_STREAM MUST NOT send more frames on that stream. RFC 7540 §6.4.
- RST_STREAM on idle stream: connection error `PROTOCOL_ERROR`. RFC 7540 §6.4.
- RST_STREAM transitions stream to `closed`. RFC 7540 §5.1, §6.4.

### 2.5 SETTINGS (`type = 0x4`) — RFC 7540 §6.5

Frame header:

```text
length=0 or 6*N type=0x4 flags stream_id=0 payload[length]
```

Payload for non-ACK:

```text
+-------------------------------+-------------------------------+
|       Identifier (16)         |          Value (32)            |  repeated
+-------------------------------+-------------------------------+
```

ACK payload: empty, `length = 0`.

Defined flag:

- `ACK = 0x1`: acknowledges peer SETTINGS. RFC 7540 §6.5, §6.5.3.

Known setting identifiers, defaults, and validation (RFC 7540 §6.5.2):

| Identifier | ID | Default | Rule |
|---|---:|---:|---|
| `SETTINGS_HEADER_TABLE_SIZE` | `0x1` | 4096 | HPACK dynamic table max bytes |
| `SETTINGS_ENABLE_PUSH` | `0x2` | 1 | Value MUST be 0 or 1; server should send 0 if no push |
| `SETTINGS_MAX_CONCURRENT_STREAMS` | `0x3` | unlimited | Advisory stream concurrency limit |
| `SETTINGS_INITIAL_WINDOW_SIZE` | `0x4` | 65535 | Value MUST be <= 2^31-1; changes all stream windows |
| `SETTINGS_MAX_FRAME_SIZE` | `0x5` | 16384 | Value MUST be 16384..16777215 |
| `SETTINGS_MAX_HEADER_LIST_SIZE` | `0x6` | unlimited | Advisory max uncompressed header-list bytes |

Rules:

- SETTINGS always applies to the connection, never a stream. If `stream_id != 0`: connection error `PROTOCOL_ERROR`. RFC 7540 §6.5.
- Payload length MUST be multiple of 6. Otherwise connection error `FRAME_SIZE_ERROR`. RFC 7540 §6.5.
- ACK SETTINGS length MUST be 0. If ACK set and length non-zero: connection error `FRAME_SIZE_ERROR`. RFC 7540 §6.5.
- Unsupported/unknown setting identifiers MUST be ignored. RFC 7540 §6.5.2.
- Settings are processed in order with no other frame processing between values in the same SETTINGS frame. RFC 7540 §6.5.3.
- Receiver MUST acknowledge non-ACK SETTINGS after applying values by sending SETTINGS with ACK and empty payload. RFC 7540 §6.5.3.
- Sender of SETTINGS that receives ACK can rely on values being applied. If ACK not received in reasonable time, MAY issue connection error `SETTINGS_TIMEOUT`. RFC 7540 §6.5.3.

### 2.6 PING (`type = 0x6`) — RFC 7540 §6.7

Frame header:

```text
length=8 type=0x6 flags stream_id=0 payload[8]
```

Payload:

```text
+---------------------------------------------------------------+
|                                                               |
|                      Opaque Data (64)                         |
|                                                               |
+---------------------------------------------------------------+
```

Defined flag:

- `ACK = 0x1`: response to PING. RFC 7540 §6.7.

Rules:

- PING is connection-level. If `stream_id != 0`: connection error `PROTOCOL_ERROR`. RFC 7540 §6.7.
- Length MUST be exactly 8. Otherwise connection error `FRAME_SIZE_ERROR`. RFC 7540 §6.7.
- Receiver of PING without ACK MUST send PING with ACK and identical opaque data. RFC 7540 §6.7.
- PING with ACK MUST NOT be responded to. RFC 7540 §6.7.

### 2.7 GOAWAY (`type = 0x7`) — RFC 7540 §6.8

Frame header:

```text
length>=8 type=0x7 flags=0 stream_id=0 payload[length]
```

Payload:

```text
+-+-------------------------------------------------------------+
|R|                  Last-Stream-ID (31)                         |  4 bytes
+-+-------------------------------------------------------------+
|                      Error Code (32)                           |  4 bytes
+---------------------------------------------------------------+
|                  Additional Debug Data (*)                     |
+---------------------------------------------------------------+
```

Defined flags: none. Undefined flags MUST be ignored. RFC 7540 §4.1, §6.8.

Rules:

- GOAWAY is connection-level. If `stream_id != 0`: connection error `PROTOCOL_ERROR`. RFC 7540 §6.8.
- Length MUST be at least 8. Otherwise connection error `FRAME_SIZE_ERROR`. RFC 7540 §6.8.
- `Last-Stream-ID` identifies highest-numbered stream sender might have acted on. Streams higher than this were not/will not be processed and can be retried. RFC 7540 §6.8.
- Receiver of GOAWAY MUST NOT open new streams on that connection. RFC 7540 §6.8.
- Endpoint can send GOAWAY with `NO_ERROR` for graceful shutdown. RFC 7540 §6.8.
- Debug data is opaque, not meant for application semantics. RFC 7540 §6.8.

### 2.8 WINDOW_UPDATE (`type = 0x8`) — RFC 7540 §6.9

Frame header:

```text
length=4 type=0x8 flags=0 stream_id=0-or-n payload[4]
```

Payload:

```text
+-+-------------------------------------------------------------+
|R|              Window Size Increment (31)                      |
+-+-------------------------------------------------------------+
```

Defined flags: none. Undefined flags MUST be ignored. RFC 7540 §4.1, §6.9.

Rules:

- `stream_id == 0`: updates connection-level flow-control window. `stream_id != 0`: updates stream-level window. RFC 7540 §6.9.
- Length MUST be exactly 4. Otherwise connection error `FRAME_SIZE_ERROR`. RFC 7540 §6.9.
- Increment MUST be 1..2^31-1. Increment 0: stream error `PROTOCOL_ERROR` for stream frame, connection error `PROTOCOL_ERROR` for connection frame. RFC 7540 §6.9.
- Flow-control window MUST NOT exceed 2^31-1. Overflow: stream error `FLOW_CONTROL_ERROR` for stream window, connection error `FLOW_CONTROL_ERROR` for connection window. RFC 7540 §6.9.1.
- Receiver can send WINDOW_UPDATE for closed streams briefly after DATA/RST race. Sender MUST ignore WINDOW_UPDATE for closed stream. RFC 7540 §6.9.
- WINDOW_UPDATE is hop-by-hop and applies only to DATA frames. RFC 7540 §5.2, §6.9.

### 2.9 CONTINUATION (`type = 0x9`) — RFC 7540 §6.10

Frame header:

```text
length[3] type=0x9 flags stream_id!=0 payload[length]
```

Payload:

```text
+---------------------------------------------------------------+
|                Header Block Fragment (*)                      |
+---------------------------------------------------------------+
```

Defined flag:

- `END_HEADERS = 0x4`: ends header block sequence. RFC 7540 §6.10.

Rules:

- CONTINUATION MUST be associated with a stream. If `stream_id == 0`: connection error `PROTOCOL_ERROR`. RFC 7540 §6.10.
- CONTINUATION MUST be preceded by HEADERS/PUSH_PROMISE/CONTINUATION without END_HEADERS on same stream. Otherwise connection error `PROTOCOL_ERROR`. RFC 7540 §6.10.
- While waiting for CONTINUATION, receiver MUST treat any other frame type or different stream as connection error `PROTOCOL_ERROR`. RFC 7540 §4.3, §6.10.
- Header block fragments are concatenated then HPACK-decoded. RFC 7540 §4.3.

## 3. h2c startup and connection preface

### 3.1 Prior-knowledge h2c (`curl --http2-prior-knowledge`, `nghttp`) — RFC 7540 §3.4, §3.5

Client connects over cleartext TCP and sends the client connection preface immediately:

```text
ASCII: "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
HEX:   50 52 49 20 2a 20 48 54 54 50 2f 32 2e 30 0d 0a 0d 0a 53 4d 0d 0a 0d 0a
LEN:   24 bytes
```

This 24-byte sequence MUST be followed by a SETTINGS frame, possibly empty. RFC 7540 §3.5.

Minimal prior-knowledge state machine:

```text
TCP_ACCEPTED
  -> read exactly 24 bytes
  -> if bytes != preface: connection error PROTOCOL_ERROR; GOAWAY MAY be omitted
  -> read frames; first client frame MUST be SETTINGS (may be empty)
  -> send server connection preface: SETTINGS frame as first server HTTP/2 frame
  -> apply client SETTINGS, send SETTINGS ACK
  -> when receiving server SETTINGS ACK from client, mark our settings acknowledged
  -> process client HEADERS/DATA frames
```

Client may send additional frames immediately after its preface and SETTINGS without waiting for server SETTINGS. Server must parse them but apply received SETTINGS in order. RFC 7540 §3.5, §6.5.3.

Empty SETTINGS frame bytes:

```text
00 00 00 04 00 00 00 00 00
|length=0|type=4|flags=0|R+stream_id=0|
```

SETTINGS ACK bytes:

```text
00 00 00 04 01 00 00 00 00
|length=0|type=4|flags=ACK|R+stream_id=0|
```

### 3.2 HTTP/1.1 Upgrade h2c — RFC 7540 §3.2, §3.2.1, §3.5

Client sends HTTP/1.1 request with exactly one `HTTP2-Settings` header:

```http
GET / HTTP/1.1
Host: server.example.com
Connection: Upgrade, HTTP2-Settings
Upgrade: h2c
HTTP2-Settings: <base64url SETTINGS payload without padding>

```

Rules:

- Client request MUST include `Upgrade: h2c` and exactly one `HTTP2-Settings`. RFC 7540 §3.2, §3.2.1.
- `HTTP2-Settings` value is base64url encoding of a SETTINGS payload, not including frame header. Trailing `=` omitted. RFC 7540 §3.2.1.
- Client MUST also list `HTTP2-Settings` in `Connection`. RFC 7540 §3.2.1.
- Server MUST NOT upgrade if HTTP2-Settings missing or repeated. RFC 7540 §3.2.1.
- Server MUST ignore `h2` token in Upgrade, because `h2` means TLS ALPN HTTP/2. RFC 7540 §3.2, §3.3.
- If accepting, server replies:

```http
HTTP/1.1 101 Switching Protocols
Connection: Upgrade
Upgrade: h2c

```

- After blank line, server's first HTTP/2 frame MUST be SETTINGS. RFC 7540 §3.2, §3.5.
- After receiving 101, client MUST send client preface: 24-byte magic + SETTINGS. RFC 7540 §3.2, §3.5.
- The HTTP/1.1 upgrade request is stream 1, default priority, half-closed from client to server. Response to that request is sent on stream 1. RFC 7540 §3.2.
- Explicit ACK of HTTP2-Settings is unnecessary; 101 response implicitly acknowledges those settings. RFC 7540 §3.2.1.

## 4. Stream state machine — RFC 7540 §5.1

Stream IDs: client-initiated streams are odd; server-initiated streams are even. Stream 0 is not a stream. A new stream ID greater than prior IDs implicitly closes skipped idle lower IDs from same initiator. RFC 7540 §5.1.1.

States:

```text
idle
reserved (local)
reserved (remote)
open
half-closed (local)
half-closed (remote)
closed
```

Transition triggers, matching RFC Figure 2:

```text
idle
  send HEADERS                 -> open
  recv HEADERS                 -> open
  send HEADERS + END_STREAM    -> half-closed (local)
  recv HEADERS + END_STREAM    -> half-closed (remote)
  send PUSH_PROMISE            -> reserved (local)
  recv PUSH_PROMISE            -> reserved (remote)
  send/recv PRIORITY           -> idle (no state change)
  recv DATA/RST_STREAM/etc     -> connection error PROTOCOL_ERROR unless allowed by section

reserved (local)
  send HEADERS                 -> half-closed (remote)
  send RST_STREAM              -> closed
  recv RST_STREAM              -> closed
  recv HEADERS/DATA            -> connection error PROTOCOL_ERROR
  send/recv PRIORITY           -> reserved (local)

reserved (remote)
  recv HEADERS                 -> half-closed (local)
  send RST_STREAM              -> closed
  recv RST_STREAM              -> closed
  send HEADERS/DATA            -> connection error PROTOCOL_ERROR
  send/recv PRIORITY           -> reserved (remote)

open
  send END_STREAM              -> half-closed (local)
  recv END_STREAM              -> half-closed (remote)
  send RST_STREAM              -> closed
  recv RST_STREAM              -> closed
  send/recv HEADERS/DATA       -> open, subject to ordering and HTTP semantics
  send/recv PRIORITY           -> open

half-closed (local)
  recv END_STREAM              -> closed
  send RST_STREAM              -> closed
  recv RST_STREAM              -> closed
  recv DATA/HEADERS            -> half-closed (local)
  send DATA/HEADERS            -> stream error STREAM_CLOSED
  send/recv PRIORITY           -> half-closed (local)

half-closed (remote)
  send END_STREAM              -> closed
  send RST_STREAM              -> closed
  recv RST_STREAM              -> closed
  send DATA/HEADERS            -> half-closed (remote)
  recv DATA/HEADERS            -> stream error STREAM_CLOSED
  send/recv PRIORITY           -> half-closed (remote)

closed
  send/recv PRIORITY           -> closed; allowed for prioritization tree
  recv WINDOW_UPDATE shortly after RST/DATA race -> ignore
  recv other frames            -> normally stream error STREAM_CLOSED; see RFC §5.1 exceptions
```

Notes:

- CONTINUATION frames do not cause stream state transitions; they are part of the preceding HEADERS/PUSH_PROMISE. RFC 7540 §5.1.
- END_STREAM can appear on HEADERS or DATA. RFC 7540 §5.1, §6.1, §6.2.
- RST_STREAM causes immediate transition to closed. RFC 7540 §5.1, §6.4.

## 5. Flow control — RFC 7540 §5.2, §6.9

Scope:

- Flow control is per-hop, not end-to-end. RFC 7540 §5.2.1.
- Applies only to DATA frames. Other frame types are not flow-controlled. RFC 7540 §5.2, §6.9.
- Two windows exist: one connection-level window (`stream_id=0`) and one stream-level window per stream. A DATA frame consumes both. RFC 7540 §5.2, §6.9.1.
- Initial window size for both connection and streams: 65,535 bytes. RFC 7540 §5.2.1, §6.9.2.
- New streams use current `SETTINGS_INITIAL_WINDOW_SIZE` as initial stream window. RFC 7540 §6.9.2.

Receiver responsibilities:

- Receiver advertises credit by sending WINDOW_UPDATE for connection and/or stream. RFC 7540 §6.9.
- Receiver MUST account DATA payload including padding against windows. RFC 7540 §6.1, §5.2.
- If sender violates advertised window: receiver MUST treat as connection error `FLOW_CONTROL_ERROR`. RFC 7540 §5.2.1.

Sender responsibilities:

- MUST NOT send DATA if either connection or stream outbound window is insufficient. RFC 7540 §5.2.1.
- May send zero-length DATA with END_STREAM even with no window. RFC 7540 §5.2.1.
- On WINDOW_UPDATE, increase outbound connection/stream window by increment. RFC 7540 §6.9.

`SETTINGS_INITIAL_WINDOW_SIZE` handling:

- Changing this setting adjusts all existing stream flow-control windows by `(new_value - old_value)`. RFC 7540 §6.9.2.
- Window can become negative after a reduction; sender MUST NOT send DATA until window becomes positive via WINDOW_UPDATE. RFC 7540 §6.9.2.
- Value MUST be <= 2^31-1. Larger: connection error `FLOW_CONTROL_ERROR`. RFC 7540 §6.5.2.
- Flow-control window MUST NOT exceed 2^31-1. Overflow: `FLOW_CONTROL_ERROR`. RFC 7540 §6.9.1.

## 6. Error codes — RFC 7540 §7

| Name | Code | Meaning |
|---|---:|---|
| `NO_ERROR` | `0x0` | Graceful shutdown / no error |
| `PROTOCOL_ERROR` | `0x1` | Unspecific protocol error |
| `INTERNAL_ERROR` | `0x2` | Unexpected internal error |
| `FLOW_CONTROL_ERROR` | `0x3` | Flow-control protocol violation |
| `SETTINGS_TIMEOUT` | `0x4` | Peer did not ACK SETTINGS in timely manner |
| `STREAM_CLOSED` | `0x5` | Frame received for already-closed stream |
| `FRAME_SIZE_ERROR` | `0x6` | Invalid frame size |
| `REFUSED_STREAM` | `0x7` | Stream refused before application processing |
| `CANCEL` | `0x8` | Stream no longer needed |
| `COMPRESSION_ERROR` | `0x9` | Header compression state cannot be maintained |
| `CONNECT_ERROR` | `0xa` | TCP connection error for CONNECT |
| `ENHANCE_YOUR_CALM` | `0xb` | Peer generating excessive load |
| `INADEQUATE_SECURITY` | `0xc` | Underlying transport security inadequate |
| `HTTP_1_1_REQUIRED` | `0xd` | Use HTTP/1.1 instead |

Unknown/unsupported error codes can be treated as equivalent to `INTERNAL_ERROR` for reporting, but code value is carried on wire as 32-bit opaque error code. RFC 7540 §7.

## 7. HPACK essentials — RFC 7541

HTTP/2 header blocks use HPACK. Static table entries are indices 1..61. Dynamic table entries start at 62. RFC 7541 §2.3.1, §2.3.3, Appendix A.

Decoder rules:

- Header fields are decoded in order. RFC 7541 §3.1, §3.2.
- Index 0 is invalid for indexed representation. RFC 7541 §6.1.
- Index greater than static+dynamic table length is decoding error. RFC 7541 §2.3.3.
- Integer encodings exceeding implementation limits are decoding errors. RFC 7541 §5.1.
- Huffman padding >7 bits, padding not EOS-prefix, or EOS symbol in string is decoding error. RFC 7541 §5.2.
- Dynamic table size update uses `001xxxxx` prefix and must appear at beginning of first header block after table size change. RFC 7541 §4.2, §6.3.

### 7.1 HPACK static table — RFC 7541 Appendix A

| Index | Name | Value |
|---:|---|---|
| 1 | `:authority` | `` |
| 2 | `:method` | `GET` |
| 3 | `:method` | `POST` |
| 4 | `:path` | `/` |
| 5 | `:path` | `/index.html` |
| 6 | `:scheme` | `http` |
| 7 | `:scheme` | `https` |
| 8 | `:status` | `200` |
| 9 | `:status` | `204` |
| 10 | `:status` | `206` |
| 11 | `:status` | `304` |
| 12 | `:status` | `400` |
| 13 | `:status` | `404` |
| 14 | `:status` | `500` |
| 15 | `accept-charset` | `` |
| 16 | `accept-encoding` | `gzip, deflate` |
| 17 | `accept-language` | `` |
| 18 | `accept-ranges` | `` |
| 19 | `accept` | `` |
| 20 | `access-control-allow-origin` | `` |
| 21 | `age` | `` |
| 22 | `allow` | `` |
| 23 | `authorization` | `` |
| 24 | `cache-control` | `` |
| 25 | `content-disposition` | `` |
| 26 | `content-encoding` | `` |
| 27 | `content-language` | `` |
| 28 | `content-length` | `` |
| 29 | `content-location` | `` |
| 30 | `content-range` | `` |
| 31 | `content-type` | `` |
| 32 | `cookie` | `` |
| 33 | `date` | `` |
| 34 | `etag` | `` |
| 35 | `expect` | `` |
| 36 | `expires` | `` |
| 37 | `from` | `` |
| 38 | `host` | `` |
| 39 | `if-match` | `` |
| 40 | `if-modified-since` | `` |
| 41 | `if-none-match` | `` |
| 42 | `if-range` | `` |
| 43 | `if-unmodified-since` | `` |
| 44 | `last-modified` | `` |
| 45 | `link` | `` |
| 46 | `location` | `` |
| 47 | `max-forwards` | `` |
| 48 | `proxy-authenticate` | `` |
| 49 | `proxy-authorization` | `` |
| 50 | `range` | `` |
| 51 | `referer` | `` |
| 52 | `refresh` | `` |
| 53 | `retry-after` | `` |
| 54 | `server` | `` |
| 55 | `set-cookie` | `` |
| 56 | `strict-transport-security` | `` |
| 57 | `transfer-encoding` | `` |
| 58 | `user-agent` | `` |
| 59 | `vary` | `` |
| 60 | `via` | `` |
| 61 | `www-authenticate` | `` |

## 8. Minimal C11 server interop checklist

For `curl --http2-prior-knowledge` and `nghttp` cleartext interop:

1. Accept TCP; verify 24-byte preface; require first frame SETTINGS. RFC 7540 §3.4, §3.5.
2. Send server SETTINGS as first frame. Empty is valid; for simple server consider `SETTINGS_ENABLE_PUSH=0`. RFC 7540 §3.5, §6.5.2.
3. ACK client SETTINGS after applying. RFC 7540 §6.5.3.
4. Parse HEADERS + CONTINUATION, HPACK decode, validate pseudo-headers. RFC 7540 §4.3, §8.1.2; RFC 7541.
5. Parse DATA, decrement flow windows, honor END_STREAM. RFC 7540 §6.1, §5.2.
6. Reply HEADERS with `:status`, END_HEADERS, then DATA frames; END_STREAM on final HEADERS or DATA. RFC 7540 §8.1.
7. Respond to PING without ACK by echoing ACK. RFC 7540 §6.7.
8. Handle WINDOW_UPDATE for outbound DATA windows. RFC 7540 §6.9.
9. Handle RST_STREAM and GOAWAY by closing stream/connection state. RFC 7540 §6.4, §6.8.
10. Ignore unknown frame types and unknown settings; ignore undefined flags. RFC 7540 §4.1, §5.5, §6.5.2.
