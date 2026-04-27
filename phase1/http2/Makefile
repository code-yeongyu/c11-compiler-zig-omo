include ../Makefile.common

override CPPFLAGS += -Iinclude
override CFLAGS += -MMD -MP

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
DEPS := \
  $(BUILD_DIR)/h2d.d \
  $(BUILD_DIR)/frame_test.d \
  $(BUILD_DIR)/hpack_test.d \
  $(BUILD_DIR)/stream_test.d \
  $(BUILD_DIR)/connection_test.d \
  $(BUILD_DIR)/e2e_test.d

ifeq ($(PLATFORM),linux)
  LINUX_TESTS := $(BUILD_DIR)/io_uring_test
  DEPS += $(BUILD_DIR)/io_uring_test.d
endif

.PHONY: all test e2e cli cflags-smoke makefile-test smoke clean print-backend

all: $(BUILD_DIR)/h2d

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/h2d: $(SERVER_SRCS) $(BACKEND_SRC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(BACKEND_CFLAGS) -MF $(BUILD_DIR)/h2d.d -MT $@ $(SERVER_SRCS) $(BACKEND_SRC) -o $@ $(LDFLAGS)

$(BUILD_DIR)/frame_test: tests/frame_test.c src/frame.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MF $(BUILD_DIR)/frame_test.d -MT $@ tests/frame_test.c src/frame.c -o $@ $(LDFLAGS)

$(BUILD_DIR)/hpack_test: tests/hpack_test.c src/frame.c src/hpack_static.c src/hpack.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MF $(BUILD_DIR)/hpack_test.d -MT $@ tests/hpack_test.c src/frame.c src/hpack_static.c src/hpack.c -o $@ $(LDFLAGS)

$(BUILD_DIR)/stream_test: tests/stream_test.c src/stream.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MF $(BUILD_DIR)/stream_test.d -MT $@ tests/stream_test.c src/stream.c -o $@ $(LDFLAGS)

$(BUILD_DIR)/connection_test: tests/connection_test.c $(TEST_COMMON) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MF $(BUILD_DIR)/connection_test.d -MT $@ tests/connection_test.c $(TEST_COMMON) -o $@ $(LDFLAGS)

$(BUILD_DIR)/e2e_test: tests/e2e_test.c $(TEST_COMMON) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MF $(BUILD_DIR)/e2e_test.d -MT $@ tests/e2e_test.c $(TEST_COMMON) -o $@ $(LDFLAGS)

$(BUILD_DIR)/io_uring_test: tests/io_uring_test.c src/io_uring.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DHTTP2_BACKEND_IO_URING -MF $(BUILD_DIR)/io_uring_test.d -MT $@ tests/io_uring_test.c -o $@ $(LDFLAGS)

makefile-test:
	@case "$$($(MAKE) -n cflags-smoke)" in *"-std=$(CSTD)"*"-fsanitize=address"*) ;; *) echo "cflags-smoke must compose base CFLAGS with ASan"; exit 1;; esac

test: $(BUILD_DIR)/frame_test $(BUILD_DIR)/hpack_test $(BUILD_DIR)/stream_test $(BUILD_DIR)/connection_test $(BUILD_DIR)/e2e_test $(LINUX_TESTS) makefile-test
	$(BUILD_DIR)/frame_test
	$(BUILD_DIR)/hpack_test
	$(BUILD_DIR)/stream_test
	$(BUILD_DIR)/connection_test
	$(LINUX_TESTS)

e2e: $(BUILD_DIR)/e2e_test
	bash tests/e2e_test.sh

cli: $(BUILD_DIR)/h2d
	bash tests/cli_test.sh

cflags-smoke:
	$(MAKE) clean
	$(MAKE) CFLAGS='$(CFLAGS) -fsanitize=address' all

smoke: $(BUILD_DIR)/h2d
	bash tests/smoke.sh

print-backend:
	@echo "PLATFORM=$(PLATFORM) IO=$(IO) BACKEND_SRC=$(BACKEND_SRC) BACKEND_CFLAGS=$(BACKEND_CFLAGS)"

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
