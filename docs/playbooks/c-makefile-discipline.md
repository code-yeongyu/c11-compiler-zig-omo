# C Makefile Discipline Playbook

This playbook codifies the discipline expected of every hand-written `Makefile` in this repository. It exists because cubic-dev-ai and reviewer-ultrabrain repeatedly surfaced the same class of issues across `phase1/doom/Makefile`, `phase1/http2/Makefile`, and `phase1/c11-ref/Makefile`:

- header changes not triggering object rebuilds (`.d` files missing or `-MMD -MP` dropped on external `CFLAGS=`),
- test rules listing only the test source as a prerequisite while the recipe also compiles an implementation file,
- generated stderr files referenced by a target that never produces them.

Every sub-Makefile contributor MUST read this document and run the checklist at the end before opening a PR.

## 1. Auto-Dependency Generation (the `-MMD -MP` Idiom)

**Rule**: every compilation rule that produces a `.o` MUST also emit a `.d` file, and the Makefile MUST include those `.d` files. Headers must trigger rebuilds.

### The idiom

Adapted from [Paul D. Smith, *Advanced Auto-Dependency Generation*](https://make.mad-scientist.net/papers/advanced-auto-dependency-generation/):

```make
DEPDIR    := .deps
DEPFLAGS  = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

%.o : %.c $(DEPDIR)/%.d | $(DEPDIR)
	$(CC) $(DEPFLAGS) $(CFLAGS) -c $< -o $@

$(DEPDIR): ; @mkdir -p $@

DEPFILES := $(SRCS:%.c=$(DEPDIR)/%.d)
$(DEPFILES):
-include $(wildcard $(DEPFILES))
```

### The `override` rule (CRITICAL)

If you write `CFLAGS += -MMD -MP`, **a user passing `make CFLAGS='-O2 -fsanitize=address'` will lose `-MMD -MP`**, because the command-line `CFLAGS=` replaces the in-Makefile `CFLAGS` definition. Use the `override` directive to make the append unconditional:

```make
override CFLAGS += -MMD -MP
override CPPFLAGS += -Iinclude
```

This is the same fix pattern as the project-wide `EXTRA_CFLAGS += -Iinclude` bug surfaced on PR #7.

### 1.1 Override Pattern: Origin Guard + Assignment Form

**Rule**: when a user passes `make CFLAGS=-O0 something`, the Makefile MUST preserve the user's flags while still prepending the build-essential base flags, and it MUST NOT accumulate duplicates under recursive `$(MAKE)` invocations.

#### The problem

When a user passes `make CFLAGS=-O0 something`, the user's `CFLAGS` arrives via either *command line* or *environment* origin. If the Makefile naively uses:

```make
# WRONG: user's -O0 is dropped because CFLAGS is replaced by the assignment.
CFLAGS := $(CFLAGS_BASE) $(CFLAGS_PLATFORM)
```

the user's `-O0` is not seen by the compile recipe. If the Makefile instead uses:

```make
# WRONG: under recursive $(MAKE) invocations, += re-evaluates and accumulates.
override CFLAGS += $(CFLAGS_BASE)
```

then under recursive `make` invocations (e.g. `$(MAKE) something` inside a recipe) the `+=` re-evaluates and `$(CFLAGS_BASE)` accumulates: each recursion appends another copy of `-std=c11 -pedantic -Wall ...`, growing the command line until make crashes or the build slows.

#### The pattern

Use `$(origin CFLAGS)` to detect whether the user explicitly passed the variable, then branch between a safe default assignment and a single-shot override assignment:

```make
ifeq ($(filter command line environment,$(origin CFLAGS)),)
CFLAGS ?= $(CFLAGS_BASE) $(CFLAGS_PLATFORM) $(EXTRA_CFLAGS)
else
override CFLAGS := $(CFLAGS) $(CFLAGS_BASE) $(CFLAGS_PLATFORM) $(EXTRA_CFLAGS)
endif
```

#### Why this works

`$(origin CFLAGS)` returns the source of the variable: `command line`, `environment`, `file`, `default`, `automatic`, `override`, etc. The `$(filter command line environment,...)` test detects whether the user explicitly passed `CFLAGS`. If NOT, we use the safe `?=` (assign-if-undefined) form so we don't clobber a sub-make. If YES, we use `override CFLAGS := ...` (assignment, not append). The single `:=` evaluation captures the user's flags AND prepends our base flags exactly once — even under recursive sub-makes the same evaluation runs once because `:=` is right-hand evaluated at parse time.

#### `+=` vs `:=` semantics

Under recursive expansion `=` and `+=` re-evaluate every time `$(CFLAGS)` is referenced, while `:=` evaluates exactly once at parse time. Combined with the `$(origin)` guard, the `:=` form is idempotent across `$(MAKE)` recursion.

#### Anti-patterns

- `override CFLAGS += $(CFLAGS_BASE)` — accumulates across recursions.
- `CFLAGS = $(CFLAGS_BASE) $(EXTRA_CFLAGS)` — drops user's `make CFLAGS=-O0`.
- `CFLAGS ?= $(CFLAGS_BASE)` without override branch — silently ignores user's `CFLAGS` in the *file* origin path.

#### Apply same pattern to LDFLAGS

PR #12's Makefile lines 17-21 do exactly this for `LDFLAGS` too. The pattern composes:

```make
ifeq ($(filter command line environment,$(origin LDFLAGS)),)
LDFLAGS ?= $(LDFLAGS_BASE) $(LDFLAGS_PLATFORM) $(EXTRA_LDFLAGS)
else
override LDFLAGS := $(LDFLAGS) $(LDFLAGS_BASE) $(LDFLAGS_PLATFORM) $(EXTRA_LDFLAGS)
endif
```

#### Reference

PR #12 `cc91d03e0bb0a128cd102b30022bbc8d8b6807ee` introduced the `$(origin)` guard and `override :=` assignment form in `phase1/c11-ref/Makefile:11-21`. The cleanup at `0a164d4` preserved the same pattern. Both commits are post-merge in `main` at `58d95e5`.

### What each flag does

| Flag | Effect |
|---|---|
| `-MMD` | generate dependency info as a side-effect of compilation (not instead of). Omits system headers. |
| `-MP` | add a phony target for each prerequisite (header). Avoids `No rule to make target` when a header is deleted. |
| `-MF path` | write the dependency file to `path`. |
| `-MT target` | set the target name in the generated `.d`. Needed when `.o` lives in a different directory from the source. |

### Edge cases

| Problem | Solution |
|---|---|
| Deleted headers | `-MP` adds phony stubs so Make never fails. |
| First build: no `.d` files yet | `-include` (leading dash) silently ignores missing `.d`. |
| Generated headers | List as order-only prerequisites (`\| generated.h`) or as explicit prerequisites of the object rule. |
| Corrupt `.d` from interrupted compile | Write to `*.Td`, rename to `*.d` only on success. |

### Production reference: Linux kernel

The kernel uses `-Wp,-MMD,$(depfile)` plus a post-processing step (`fixdep`) to also track `CONFIG_*` symbol dependencies — see [scripts/Kbuild.include](https://github.com/torvalds/linux/blob/254f49634ee16a731174d2ae34bc50bd5f45e731/scripts/Kbuild.include).

## 2. Hand-Written Prerequisites (When and How)

**Rule**: every `.c` file that appears in a recipe MUST appear in that rule's prerequisite list.

### Anti-pattern (real example surfaced by cubic on PR #9)

```make
# WRONG: i_net_headless.c is compiled but not declared as prerequisite.
$(TEST_NET_OOM): tests/test_headless_net_oom.c | $(BUILD_DIR)/tests
	$(CC) $(CFLAGS) tests/test_headless_net_oom.c headless/i_net_headless.c -o $@
```

### Correct pattern

```make
# RIGHT: every .c in the recipe is in the prerequisite list.
$(TEST_NET_OOM): tests/test_headless_net_oom.c headless/i_net_headless.c | $(BUILD_DIR)/tests
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@
```

Use `$^` (all prerequisites, deduplicated) or `$(filter %.c,$^)` (filter out non-source) so the file list never duplicates.

### Production reference: Redis

Redis [`src/Makefile`](https://github.com/redis/redis/blob/47c51369eeffd55e1baf20df7955a3dfbe842fc4/src/Makefile) lists every multi-source recipe's `.c` files as prerequisites.

## 3. Phony Target Hygiene

**Rule**: every target that does not produce a file with the same name MUST be declared `.PHONY`.

### Idiom

```make
.PHONY: all test negative clean check
```

### Common mistakes

- Forgetting `.PHONY` on `test`, `clean`, `all`, `check`. If a file named `test` ever appears in the tree, Make will skip the recipe.
- Naming a phony target the same as a generated file. Rename the binary (`run-tests`, `check-suite`) or use a different target name.

## 4. Test-Target Dependency Tracking

A test target MUST depend on:

1. the test executable(s) it runs,
2. all implementation files compiled into those executables (via the executable's prerequisite list, not the test target's).

### Pattern for multi-source test executables

```make
TEST_SRCS := tests/frame_test.c tests/hpack_test.c tests/connection_test.c
IMPL_SRCS := src/frame.c src/hpack.c src/connection.c

# Each test binary: test source + the implementation it exercises.
$(BUILD_DIR)/%_test: tests/%_test.c $(IMPL_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ $(LDFLAGS)

# The test runner depends on the test binaries.
test: $(patsubst tests/%_test.c,$(BUILD_DIR)/%_test,$(TEST_SRCS))
	@set -e; for t in $^; do echo "==> $$t"; $$t || exit 1; done
```

### Key points

- Use `$(filter %.c,$^)` to pass only source files, excluding order-only prerequisites like `| $(BUILD_DIR)`.
- If a test binary is built from a single `.c` file (no separate `.o`), still declare all implementation files as prerequisites so the test rebuilds when the implementation changes.
- If you use `-MMD -MP` auto-dep for `.o` files, the `.o` intermediates already carry header dependencies; the test executable rule only needs to depend on the `.o` files, not the raw `.c`.

### 4.1 Parallel-Make Artifact Namespacing

**Rule**: when two recipes can produce the same artifact filename via different code paths, namespace the artifact by its source path so `make -j` cannot race.

#### The race

Consider a Makefile with both a stdin-compile rule and a path-compile rule for the same source:

```make
# WRONG: both recipes write to the same filename.
$(BUILD_DIR)/%.gcc.err: %.c | $(BUILD_DIR)
	-$(CC) $(CFLAGS) -c $< -o /dev/null 2> $@ || true

$(BUILD_DIR)/%.gcc.err: $(STDIN_DIR)/%.c | $(BUILD_DIR)
	-$(CC) $(CFLAGS) -c $< -o /dev/null 2> $@ || true
```

Running `make -j` can invoke both recipes concurrently. Whichever finishes last overwrites the artifact, and the subsequent grep may inspect the wrong diagnostic output.

#### Correct pattern

Namespace artifacts by their source path so every recipe writes to a unique filename:

```make
# RIGHT: distinct filenames per source path.
$(BUILD_DIR)/%.stdin.gcc.err: $(STDIN_DIR)/%.c | $(BUILD_DIR)
	-$(CC) $(CFLAGS) -c $< -o /dev/null 2> $@ || true

$(BUILD_DIR)/%.path.gcc.err: %.c | $(BUILD_DIR)
	-$(CC) $(CFLAGS) -c $< -o /dev/null 2> $@ || true
```

#### Before / after

```make
# BEFORE (race-prone shared filename)
$(BUILD_DIR)/atomic_filename_regression.gcc.err

# AFTER (namespaced, race-free)
$(BUILD_DIR)/atomic_filename_regression.stdin.gcc.err
$(BUILD_DIR)/atomic_filename_regression.path.gcc.err
```

**Reference**: PR #12 `80b4a4a` `atomic_filename_regression.stdin.*` namespace fix.

## 5. Negative-Test Diagnostic Verification

When a Makefile rule asserts that a compiler produced a specific diagnostic, the grep MUST be scoped to diagnostic message lines, NOT to filenames echoed by the compiler. Anti-pattern surfaced by cubic on PR #5 and PR #12:

```make
# WRONG: filename text echoed by GCC can satisfy the regex.
$(BUILD_DIR)/negative/$(name).gcc.err:
	-$(CC) $(CFLAGS) -c $< -o /dev/null 2> $@ || true
check-negative-gcc:
	grep -E "$(NEG_KEYWORDS_$$base)" $(BUILD_DIR)/negative/$$name.gcc.err
```

If a negative test source is named `_Atomic_param.c`, the line `$(SRC): error: ...` echoed by GCC contains `_Atomic_` and the grep falsely passes.

### Correct pattern

Strip the source filename from stderr before grepping the diagnostic, or grep specifically for the message portion:

```make
check-negative-gcc:
	grep -E "$(NEG_KEYWORDS_$$base)" \
	  <(grep -v "^$(<):" $(BUILD_DIR)/negative/$$name.gcc.err)
```

Add a regression fixture: a deliberately-misnamed source file (e.g. `negative/atomic_filename_regression.c`) that exercises the false-positive avoidance.

## 6. References

| Source | URL |
|---|---|
| GNU Make Manual — §4.14 *Generating Prerequisites Automatically* | https://www.gnu.org/software/make/manual/html_node/Automatic-Prerequisites.html |
| GNU Make Manual — §4.6 *Phony Targets* | https://www.gnu.org/software/make/manual/html_node/Phony-Targets.html |
| GNU Make Manual — §6.7 *The `override` Directive* | https://www.gnu.org/software/make/manual/html_node/Override-Directive.html |
| Paul D. Smith — *Advanced Auto-Dependency Generation* | https://make.mad-scientist.net/papers/advanced-auto-dependency-generation/ |
| Linux kernel — `scripts/Kbuild.include` | https://github.com/torvalds/linux/blob/254f49634ee16a731174d2ae34bc50bd5f45e731/scripts/Kbuild.include |
| Linux kernel — `scripts/Makefile.lib` | https://github.com/torvalds/linux/blob/254f49634ee16a731174d2ae34bc50bd5f45e731/scripts/Makefile.lib |
| Redis — `src/Makefile` | https://github.com/redis/redis/blob/47c51369eeffd55e1baf20df7955a3dfbe842fc4/src/Makefile |

## 7. Pre-PR Checklist for Sub-Project Owners

Before opening a Makefile-touching PR, verify:

- [ ] Every `.o` rule uses `-MMD -MP -MF $(DEPDIR)/$*.d` (or equivalent).
- [ ] `CFLAGS`, `CPPFLAGS`, `LDFLAGS`, `EXTRA_CFLAGS` additions that the build cannot succeed without are wrapped with `override`.
- [ ] Run `make clean && make CFLAGS='-O2 -fsanitize=address' all` and confirm `.d` files are still present.
- [ ] Every test/executable rule lists ALL `.c` files that appear in its recipe as prerequisites.
- [ ] All non-file targets (`all`, `test`, `clean`, `check`, `negative`) are declared `.PHONY`.
- [ ] Build directories are order-only prerequisites (`| $(BUILD_DIR)`) and the directory rule uses `mkdir -p`.
- [ ] No hand-written header dependency lines (let the compiler generate them).
- [ ] Negative-test rules grep diagnostics with the source filename stripped from stderr.
- [ ] No target that consumes a file (`< $(file)` or `cat $(file)`) without the file being a prerequisite or being created earlier in the same recipe.
- [ ] No two recipes write to the same artifact filename via different code paths (parallel-make race safety).
- [ ] CFLAGS/LDFLAGS use the $(origin) guard pattern with override := (assignment form), not bare override +=
- [ ] Test: `make CFLAGS=-O0 all` (or any build target) preserves `-O0` AND prepends base flags; `make CFLAGS=-O0 CFLAGS_BASE=foo all` shows both.
