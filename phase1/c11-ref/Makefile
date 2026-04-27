COMMON := ../Makefile.common

ifneq (,$(wildcard $(COMMON)))
include $(COMMON)
else
CSTD ?= c11
CFLAGS_BASE ?= -std=$(CSTD) -pedantic -Wall -Wextra -Werror -O2 -g
endif

ifeq ($(filter command line environment,$(origin CFLAGS)),)
CFLAGS ?= $(CFLAGS_BASE) $(CFLAGS_PLATFORM) $(EXTRA_CFLAGS)
else
override CFLAGS := $(CFLAGS) $(CFLAGS_BASE) $(CFLAGS_PLATFORM) $(EXTRA_CFLAGS)
endif

ifeq ($(filter command line environment,$(origin LDFLAGS)),)
LDFLAGS ?= $(LDFLAGS_BASE) $(LDFLAGS_PLATFORM) $(EXTRA_LDFLAGS)
else
override LDFLAGS := $(LDFLAGS) $(LDFLAGS_BASE) $(LDFLAGS_PLATFORM) $(EXTRA_LDFLAGS)
endif

PROGRAMS := \
	001_generic_selection \
	002_alignas_alignof \
	003_atomic_qualifier \
	004_atomic_specifier \
	005_thread_local \
	006_noreturn \
	007_static_assert \
	008_anonymous_struct_union \
	009_unicode_literals \
	010_designated_initializers \
	011_compound_literals \
	012_complex_arithmetic \
	013_vla \
	014_aligned_alloc \
	015_static_assert_runtime_mix \
	016_optional_feature_macros \
	017_integer_promotions \
	018_usual_arithmetic_conversions \
	019_corner_cases \
	020_gets_removal \
	021_fopen_x_mode \
	022_quick_exit \
	023_timespec_get

TESTS := $(patsubst %.c,%,$(wildcard *_test.c))
ALL_C := $(sort $(wildcard *.c))
NEGATIVE_SRCS := $(sort $(wildcard negative/*.c))
GCC ?= gcc
CLANG ?= clang
BUILD_DIR := .build
TEST_CC ?= $(CC)
LDLIBS ?= -lm
COMMON_PREREQ := $(wildcard $(COMMON))
DEPS := $(ALL_C:%=$(BUILD_DIR)/gcc/%.d) $(ALL_C:%=$(BUILD_DIR)/clang/%.d) $(TESTS:%=$(BUILD_DIR)/%.d)
NEGATIVE_CFLAGS := $(CFLAGS) -pedantic-errors

.PHONY: all test clean check-gcc check-clang negative check-negative-gcc check-negative-clang check-negative-filename-regression
.NOTPARALLEL:

all: check-gcc negative check-clang $(TESTS)

check-gcc: $(ALL_C:%=$(BUILD_DIR)/gcc/%.o)

check-clang:
	@if command -v $(CLANG) >/dev/null 2>&1; then \
		$(MAKE) $(ALL_C:%=$(BUILD_DIR)/clang/%.o) check-negative-clang; \
	else \
		printf 'clang not found; skipping clang syntax build\n'; \
	fi

negative: check-negative-gcc check-negative-filename-regression

check-negative-gcc: $(NEGATIVE_SRCS)
	@mkdir -p $(BUILD_DIR)/negative
	@set -e; for src in $(NEGATIVE_SRCS); do \
		name=$$(basename $$src .c); \
		keyword='error'; \
		if [ "$$name" = atomic_array ]; then keyword='atomic|_Atomic|array'; fi; \
		if [ "$$name" = atomic_filename_regression ]; then keyword='expected|expression'; fi; \
		if [ "$$name" = alignas_bitfield ]; then keyword='align|Alignas|bit-field|field'; fi; \
		if [ "$$name" = vla_in_struct ]; then keyword='variable|variably|VLA|field|member'; fi; \
		if $(GCC) $(NEGATIVE_CFLAGS) -x c -c - -o $(BUILD_DIR)/negative/$$name.gcc.o < $$src 2>$(BUILD_DIR)/negative/$$name.gcc.err; then \
			printf '%s unexpectedly compiled with gcc\n' $$src >&2; \
			exit 1; \
		fi; \
		grep -E '(^| )error:|warning:' $(BUILD_DIR)/negative/$$name.gcc.err | grep -E "$$keyword" >/dev/null || { \
			printf '%s gcc diagnostic missed expected constraint keyword\n' $$src >&2; \
			exit 1; \
		}; \
		printf 'OK negative gcc: %s rejected as expected\n' $$src; \
	done

check-negative-clang: $(NEGATIVE_SRCS)
	@mkdir -p $(BUILD_DIR)/negative
	@set -e; for src in $(NEGATIVE_SRCS); do \
		name=$$(basename $$src .c); \
		keyword='error'; \
		if [ "$$name" = atomic_array ]; then keyword='atomic|_Atomic|array'; fi; \
		if [ "$$name" = atomic_filename_regression ]; then keyword='expected|expression'; fi; \
		if [ "$$name" = alignas_bitfield ]; then keyword='align|Alignas|bit-field|field'; fi; \
		if [ "$$name" = vla_in_struct ]; then keyword='variable|variably|VLA|field|member'; fi; \
		if $(CLANG) $(NEGATIVE_CFLAGS) -x c -c - -o $(BUILD_DIR)/negative/$$name.clang.o < $$src 2>$(BUILD_DIR)/negative/$$name.clang.err; then \
			printf '%s unexpectedly compiled with clang\n' $$src >&2; \
			exit 1; \
		fi; \
		grep -E '(^| )error:|warning:' $(BUILD_DIR)/negative/$$name.clang.err | grep -E "$$keyword" >/dev/null || { \
			printf '%s clang diagnostic missed expected constraint keyword\n' $$src >&2; \
			exit 1; \
		}; \
		printf 'OK negative clang: %s rejected as expected\n' $$src; \
	done

check-negative-filename-regression:
	@mkdir -p $(BUILD_DIR)/negative
	@src=negative/atomic_filename_regression.c; \
	if $(GCC) $(NEGATIVE_CFLAGS) -x c -c - -o $(BUILD_DIR)/negative/atomic_filename_regression.stdin.gcc.o < $$src 2>$(BUILD_DIR)/negative/atomic_filename_regression.stdin.gcc.err; then \
		printf '%s unexpectedly compiled with gcc\n' $$src >&2; \
		exit 1; \
	fi; \
	if $(GCC) $(NEGATIVE_CFLAGS) -c $$src -o $(BUILD_DIR)/negative/atomic_filename_regression.path.gcc.o 2>$(BUILD_DIR)/negative/atomic_filename_regression.path.gcc.err; then \
		printf '%s unexpectedly compiled with gcc\n' $$src >&2; \
		exit 1; \
	fi; \
	grep -E 'atomic|_Atomic|array' $(BUILD_DIR)/negative/atomic_filename_regression.path.gcc.err >/dev/null || { \
		printf '%s gcc path diagnostic no longer demonstrates filename keyword pollution\n' $$src >&2; \
		exit 1; \
	}; \
	if grep -E '(^| )error:|warning:' $(BUILD_DIR)/negative/atomic_filename_regression.stdin.gcc.err | grep -E 'atomic|_Atomic|array' >/dev/null; then \
		printf '%s gcc diagnostic matched keyword only from source content, expected no atomic keyword\n' $$src >&2; \
		exit 1; \
	fi; \
	printf 'OK negative gcc: filename keyword regression rejected as expected\n'; \
	if command -v $(CLANG) >/dev/null 2>&1; then \
		if $(CLANG) $(NEGATIVE_CFLAGS) -x c -c - -o $(BUILD_DIR)/negative/atomic_filename_regression.stdin.clang.o < $$src 2>$(BUILD_DIR)/negative/atomic_filename_regression.stdin.clang.err; then \
			printf '%s unexpectedly compiled with clang\n' $$src >&2; \
			exit 1; \
		fi; \
		if $(CLANG) $(NEGATIVE_CFLAGS) -c $$src -o $(BUILD_DIR)/negative/atomic_filename_regression.path.clang.o 2>$(BUILD_DIR)/negative/atomic_filename_regression.path.clang.err; then \
			printf '%s unexpectedly compiled with clang\n' $$src >&2; \
			exit 1; \
		fi; \
		grep -E 'atomic|_Atomic|array' $(BUILD_DIR)/negative/atomic_filename_regression.path.clang.err >/dev/null || { \
			printf '%s clang path diagnostic no longer demonstrates filename keyword pollution\n' $$src >&2; \
			exit 1; \
		}; \
		if grep -E '(^| )error:|warning:' $(BUILD_DIR)/negative/atomic_filename_regression.stdin.clang.err | grep -E 'atomic|_Atomic|array' >/dev/null; then \
			printf '%s clang diagnostic matched keyword only from source content, expected no atomic keyword\n' $$src >&2; \
			exit 1; \
		fi; \
		printf 'OK negative clang: filename keyword regression rejected as expected\n'; \
	fi

$(BUILD_DIR)/gcc/%.c.o: %.c $(COMMON_PREREQ)
	@mkdir -p $(dir $@)
	$(GCC) $(CFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(BUILD_DIR)/clang/%.c.o: %.c $(COMMON_PREREQ)
	@mkdir -p $(dir $@)
	$(CLANG) $(CFLAGS) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

test: negative $(TESTS)
	@set -e; for t in $(TESTS); do echo "==> $$t"; "./$$t" || exit 1; done

%_test: %_test.c %.c $(COMMON_PREREQ)
	@mkdir -p $(BUILD_DIR)
	$(TEST_CC) $(CFLAGS) -MMD -MP -MF $(BUILD_DIR)/$@.d $< -o $@ $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf $(BUILD_DIR) $(TESTS) *.dSYM

-include $(wildcard $(DEPS))
