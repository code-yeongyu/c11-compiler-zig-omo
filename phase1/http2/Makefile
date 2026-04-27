EXTRA_CFLAGS += -Iinclude

include ../Makefile.common

IO ?= epoll
BUILD_DIR := build
COMMON_SRCS := src/frame.c src/hpack_static.c src/hpack.c src/stream.c src/connection.c src/server.c
SERVER_SRCS := $(COMMON_SRCS) src/main.c

ifeq ($(PLATFORM),macos)
  BACKEND_SRC := src/io_kqueue.c
  BACKEND_CFLAGS := -DHTTP2_BACKEND_KQUEUE
else ifeq ($(PLATFORM),linux)
  ifeq ($(IO),epoll)
    BACKEND_SRC := src/io_epoll.c
    BACKEND_CFLAGS := -DHTTP2_BACKEND_EPOLL
  else ifeq ($(IO),io_uring)
    BACKEND_SRC := src/io_uring.c
    BACKEND_CFLAGS := -DHTTP2_BACKEND_IO_URING
  else
    $(error IO must be epoll or io_uring on Linux)
  endif
else
  $(error unsupported platform $(UNAME_S))
endif

TEST_COMMON := src/frame.c src/hpack_static.c src/hpack.c src/stream.c src/connection.c

.PHONY: all test smoke clean print-backend

all: $(BUILD_DIR)/h2d

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/h2d: $(SERVER_SRCS) $(BACKEND_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(BACKEND_CFLAGS) $(SERVER_SRCS) $(BACKEND_SRC) -o $@ $(LDFLAGS)

$(BUILD_DIR)/frame_test: tests/frame_test.c src/frame.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) tests/frame_test.c src/frame.c -o $@ $(LDFLAGS)

$(BUILD_DIR)/hpack_test: tests/hpack_test.c src/frame.c src/hpack_static.c src/hpack.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) tests/hpack_test.c src/frame.c src/hpack_static.c src/hpack.c -o $@ $(LDFLAGS)

$(BUILD_DIR)/connection_test: tests/connection_test.c $(TEST_COMMON) | $(BUILD_DIR)
	$(CC) $(CFLAGS) tests/connection_test.c $(TEST_COMMON) -o $@ $(LDFLAGS)

test: $(BUILD_DIR)/frame_test $(BUILD_DIR)/hpack_test $(BUILD_DIR)/connection_test
	$(BUILD_DIR)/frame_test
	$(BUILD_DIR)/hpack_test
	$(BUILD_DIR)/connection_test

smoke: $(BUILD_DIR)/h2d
	bash tests/smoke.sh

print-backend:
	@echo "PLATFORM=$(PLATFORM) IO=$(IO) BACKEND_SRC=$(BACKEND_SRC) BACKEND_CFLAGS=$(BACKEND_CFLAGS)"

clean:
	rm -rf $(BUILD_DIR)
