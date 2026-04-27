COMMON := ../Makefile.common

ifneq (,$(wildcard $(COMMON)))
include $(COMMON)
else
CSTD ?= c11
CFLAGS ?= -std=$(CSTD) -pedantic -Wall -Wextra -Werror -O2 -g
LDFLAGS ?=
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
	019_corner_cases

TESTS := $(addsuffix _test,$(PROGRAMS))
ALL_C := $(sort $(wildcard *.c))
GCC ?= gcc
CLANG ?= clang
BUILD_DIR := .build
TEST_CC ?= $(CC)
LDLIBS ?= -lm

.PHONY: all test clean check-gcc check-clang

all: check-gcc check-clang $(TESTS)

check-gcc: $(ALL_C:%=$(BUILD_DIR)/gcc/%.o)

check-clang:
	@if command -v $(CLANG) >/dev/null 2>&1; then \
		$(MAKE) $(ALL_C:%=$(BUILD_DIR)/clang/%.o); \
	else \
		printf 'clang not found; skipping clang syntax build\n'; \
	fi

$(BUILD_DIR)/gcc/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(GCC) -std=c11 -pedantic -Wall -Wextra -Werror -c $< -o $@

$(BUILD_DIR)/clang/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CLANG) -std=c11 -pedantic -Wall -Wextra -Werror -c $< -o $@

test: $(TESTS)
	@for test in $(TESTS); do ./$$test; done

%_test: %_test.c %.c
	$(TEST_CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf $(BUILD_DIR) $(TESTS) *.dSYM
