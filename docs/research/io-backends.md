# HTTP/2 Server I/O Backend Patterns

## io_uring (Linux)

### Setup

```c
#include <liburing.h>

struct io_uring ring;
int ret = io_uring_queue_init(32, &ring, 0);
if (ret < 0) {
    fprintf(stderr, "io_uring_queue_init: %s\n", strerror(-ret));
    return -1;
}
```

### IORING_OP_ACCEPT

```c
struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
io_uring_prep_accept(sqe, listen_fd, NULL, NULL, 0);
sqe->user_data = 1; // tag as accept operation
io_uring_submit(&ring);
```

### IORING_OP_RECV

```c
struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
io_uring_prep_recv(sqe, conn_fd, buf, buf_len, 0);
sqe->user_data = conn_id; // tag with connection ID
io_uring_submit(&ring);
```

### IORING_OP_SEND

```c
struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
io_uring_prep_send(sqe, conn_fd, buf, buf_len, 0);
sqe->user_data = conn_id;
io_uring_submit(&ring);
```

### CQE Harvest Loop

```c
struct io_uring_cqe *cqe;
int ret = io_uring_wait_cqe(&ring, &cqe);
if (ret < 0) {
    // error
}

if (cqe->user_data == 1) {
    // accept completed
    int new_fd = cqe->res;
} else {
    // recv/send completed for connection cqe->user_data
}

io_uring_cqe_seen(&ring, cqe);
```

## kqueue (macOS)

### Setup

```c
int kq = kqueue();
if (kq < 0) {
    perror("kqueue");
    return -1;
}
```

### Nonblocking Accept

```c
struct kevent ev;
EV_SET(&ev, listen_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
kevent(kq, &ev, 1, NULL, 0, NULL);
```

### Event Loop

```c
struct kevent events[128];
int nevents = kevent(kq, NULL, 0, events, 128, NULL);

for (int i = 0; i < nevents; i++) {
    if (events[i].ident == listen_fd) {
        // accept new connection
        int conn_fd = accept(listen_fd, NULL, NULL);
        fcntl(conn_fd, F_SETFL, O_NONBLOCK);
        
        struct kevent ev;
        EV_SET(&ev, conn_fd, EVFILT_READ, EV_ADD, 0, 0, conn_ptr);
        kevent(kq, &ev, 1, NULL, 0, NULL);
    } else {
        struct connection *conn = events[i].udata;
        if (events[i].filter == EVFILT_READ) {
            // read data
        } else if (events[i].filter == EVFILT_WRITE) {
            // write data
        }
    }
}
```

## curl h2c Testing

```bash
# Basic h2c test
curl -v --http2-prior-knowledge http://127.0.0.1:8080/

# With trace for full frame dump
curl -v --http2-prior-knowledge --trace-ascii /dev/stderr http://127.0.0.1:8080/

# nghttp client (richer diagnostics)
nghttp -v http://127.0.0.1:8080/

# Concurrent load test
curl --parallel --parallel-max 16 -s -o /dev/null --http2-prior-knowledge http://127.0.0.1:8080/{1..100}
```

## Build Flag Matrix

| Platform | Flag | Link |
|---|---|---|
| macOS | `-D IO_BACKEND_KQUEUE` | nothing extra |
| Linux (preferred) | `-D IO_BACKEND_IO_URING` | `-luring` |
| Linux (fallback) | `-D IO_BACKEND_EPOLL` | nothing extra |

Auto-detect: `pkg-config --exists liburing && echo yes`
