# Playbook — Phase-1 CI build matrix

This playbook is the **contract** every `phase1/<sub>/Makefile` must satisfy. CI ([`.github/workflows/phase1-c.yml`](../../.github/workflows/phase1-c.yml)) calls these targets directly; if your Makefile is missing one, your job will fail (or, before any subproject Makefile lands, be skipped with a `::notice::`).

## Target contract

| Target | When CI calls it | What it must do |
|---|---|---|
| `print-platform` | every job (best-effort) | Echo `PLATFORM/ARCH/CC/CFLAGS`. Provided by `phase1/Makefile.common`. |
| `all` | every job | Build the primary artefact(s). |
| `test` | every job | Run unit tests; **non-zero exit on any failure**. |
| `smoke` | http2 (all cells) + doom (native ubuntu-latest) | End-to-end smoke. Headless. Fast (under 60 s). |
| `clean` | not in CI, expected for local hygiene | Remove all generated output. |
| `qa` | optional, manual only | Interactive QA capture (tmux-recorded session) — never invoked from CI. |

A subproject Makefile **must** include `phase1/Makefile.common` and **must** define `all`, `test`, and `clean`. `smoke` is required for `http2` and `doom`; for `c11-ref` the test runner already exercises every program end-to-end so `smoke` may be aliased to `test`. `qa` is optional and only invoked by qa-tester.

## Inherited variables (from `phase1/Makefile.common`)

After `include ../Makefile.common`, the following are pre-set:

| Variable | Default | Notes |
|---|---|---|
| `CC` | `cc` | Override per CI cell: `make CC=gcc` / `make CC=clang`. |
| `CSTD` | `c11` | Subprojects can override (e.g. DOOM may need `c99` for K&R headers — document the why). |
| `CFLAGS` | strict warnings + `-O2 -g` + platform `_GNU_SOURCE`/`_DARWIN_C_SOURCE` | Append-only via `EXTRA_CFLAGS=...` on the `make` CLI. |
| `LDFLAGS` | platform-pthread on Linux, empty on macOS | Append-only via `EXTRA_LDFLAGS=...`. |
| `EXTRA_CFLAGS` / `EXTRA_LDFLAGS` | empty | The CLI escape hatch for backend / cell-specific flags. |
| `PLATFORM` | `linux` or `macos` | Auto-detected from `uname -s`. |
| `UNAME_M` | `x86_64` / `arm64` / ... | Auto-detected from `uname -m`. |

`phase1/Makefile.common` also provides `require-linux`, `require-darwin`, and `print-platform` as `.PHONY` helpers; subprojects can use `require-*` as a prerequisite to gate platform-only targets.

Subprojects pick their own build directory convention (the `.gitignore` already covers `phase1/c11-ref/build/`, `phase1/http2/build/`, `phase1/doom/build/`). Keep build outputs inside that subdirectory so `.gitignore` keeps tracked status clean.

## CI matrix (`.github/workflows/phase1-c.yml`)

| Job | Runner | Compiler | Variant |
|---|---|---|---|
| `c11-ref / native gcc on ubuntu-latest` | ubuntu-latest | gcc | — |
| `c11-ref / native clang on ubuntu-latest` | ubuntu-latest | clang | — |
| `c11-ref / native gcc on macos-latest` | macos-latest | gcc (Homebrew) | — |
| `c11-ref / native clang on macos-latest` | macos-latest | Apple clang | — |
| `http2 / macos-latest (kqueue)` | macos-latest | clang | `IO=kqueue` |
| `http2 / ubuntu-latest (epoll)` | ubuntu-latest | gcc | `IO=epoll` |
| `http2 / ubuntu-latest (io_uring)` | ubuntu-latest | gcc | `IO=io_uring` (liburing-dev) |
| `doom / headless build & smoke` | ubuntu-latest | gcc + libx11-dev + xvfb-run | `make smoke` runs under `xvfb-run -a` |
| `docker / gcc-13 / {c11-ref,http2,doom}` | ubuntu-latest | gcc:13 in docker | http2 pinned `IO=epoll`; doom skips smoke (no Xvfb in image) |
| `docker / gcc-14 / {c11-ref,http2,doom}` | ubuntu-latest | gcc:14 in docker | http2 pinned `IO=epoll`; doom skips smoke (no Xvfb in image) |

Why io_uring is **not** in the docker matrix — `gcc:13` / `gcc:14` images run unprivileged and can't `io_uring_setup(2)`. The native `ubuntu-latest` cell covers io_uring; future cross-compilation (Phase 2/3) will revisit this.

Why doom smoke is **not** in the docker matrix — bare `gcc:13/14` images don't ship Xvfb. The native ubuntu-latest job runs smoke under `xvfb-run`; the docker matrix only validates that DOOM **builds** under `gcc:13` and `gcc:14`. If a future contributor needs docker smoke too, add Xvfb to a custom image first.

## Reproducing CI locally

### macOS host, against gcc:14 in OrbStack

```bash
orb start
scripts/run-in-docker.sh c11-ref            # default targets: print-platform + all + test
scripts/run-in-docker.sh http2 all test smoke IO=epoll
scripts/run-in-docker.sh doom  all test                  # no smoke in docker (Xvfb-less image)
scripts/run-in-docker.sh c11-ref all test -- IMAGE=gcc:13   # alt image
```

### macOS host, native (matches `*-native` jobs)

```bash
make -C phase1/c11-ref CC=clang all test
make -C phase1/c11-ref CC=gcc   all test    # needs `brew install gcc`
make -C phase1/http2   IO=kqueue   all test smoke
```

### Linux host, native (matches `ubuntu-latest` jobs)

```bash
make -C phase1/c11-ref CC=gcc   all test
make -C phase1/c11-ref CC=clang all test
make -C phase1/http2   IO=epoll    all test smoke
make -C phase1/http2   IO=io_uring all test smoke
xvfb-run -a make -C phase1/doom all smoke
```

## Skeleton subproject Makefile

```make
# phase1/<sub>/Makefile
include ../Makefile.common

OBJDIR := build
BIN    := $(OBJDIR)/<sub>
SRCS   := main.c foo.c bar.c
OBJS   := $(SRCS:%.c=$(OBJDIR)/%.o)

all: $(BIN)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(BIN): $(OBJS) | $(OBJDIR)
	$(CC) $(LDFLAGS) -o $@ $^

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

test: all
	$(BIN) --self-test

smoke: all
	$(BIN) --smoke

clean:
	rm -rf $(OBJDIR)
	rm -f core.* *.log

.PHONY: all test smoke clean
```

## Common failure modes

| Symptom | Cause | Fix |
|---|---|---|
| `make: *** No rule to make target 'all'` in CI | Subproject Makefile not authored yet | The job emits a `::notice::` and exits 0 — that's expected during phase ramp-up. Once the Makefile lands, the gate flips. |
| `make: *** No rule to make target 'print-platform'` | Subproject Makefile didn't include `../Makefile.common` | Add `include ../Makefile.common` at the top. **`make print-platform` is the canonical sentinel** — if it doesn't print `PLATFORM=… ARCH=… CC=… CFLAGS=…`, the include line is missing or broken. |
| Build artefacts showing up as untracked in PRs | `OBJDIR` not under `phase1/<sub>/build/` | Either move builds into `build/`, or extend the root `.gitignore` (and document why in the PR). |
| `liburing-dev not found` in docker | Tried to set `IO=io_uring` in the docker matrix | Don't — io_uring is exercised in the native ubuntu-latest cell; the docker matrix pins http2 to `IO=epoll`. |
| `doom` builds in docker but smoke is skipped | Intentional — no Xvfb in bare gcc image | Run smoke under the native `doom / headless build & smoke` job, which installs `xvfb` and wraps `make smoke` in `xvfb-run`. |
| CI red after PR push | Author forgot CI-obsession check | Run `gh run list --repo code-yeongyu/c11-compiler-zig-omo --limit 5` within ~30 s of any push; open a fix-PR if any cell is red. |

## Reviewer-quality (`{[reviewer-quality]}`) review checklist

When reviewing a subproject PR, check:

1. Makefile includes `../Makefile.common` and defines `all`, `test`, `clean` (and `smoke` for http2/doom).
2. All targets succeed locally on **both** macOS-native and `gcc:14` docker (use `scripts/run-in-docker.sh`).
3. No build artefacts committed (`git ls-files | rg '\.(o|a|so|dylib)$'` returns empty).
4. Tests are real: unit tests **and** an end-to-end test, no mocks unless physically unavoidable, given/when/then in the body (no `Arrange-Act-Assert` comments).
5. Every cell of the CI matrix above is green.
6. PR body links the relevant tracking issue and includes a Test Plan section per the team charter.

If 1–6 pass, comment `reviewer-quality: APPROVE` on the PR. If anything fails, leave a `{[reviewer-quality]}` change-request comment with concrete diffs/log excerpts.
