# c11-compiler-zig-omo

A three-phase project:

1. **Phase 1** — reference C11 code that exercises the harder corners of the C11 spec:
   - DOOM (`phase1/doom`), built under native gcc/clang and Linux gcc-in-docker.
   - HTTP/2 server (`phase1/http2`) with auto-detected kqueue / epoll / io_uring.
   - A hand-crafted C11 conformance suite (`phase1/c11-ref`).
2. **Phase 2** — a C11 compiler in Zig 0.16+, **zero external dependencies**, exploiting async / threads / multi-process / multi-core.
3. **Phase 3** — verification: take Phase 1's C code (and a battery of edge cases), compile it with the Phase 2 compiler, debug the output with gdb + Ghidra under both native and cross-compilation targets, in `-O0` and optimized builds.

See [`docs/team/charter.md`](docs/team/charter.md) for how the team operates, and [`docs/decisions/0001-stack-and-toolchain.md`](docs/decisions/0001-stack-and-toolchain.md) for the toolchain matrix.

## Quick start

```bash
brew install zig gdb ghidra
docker pull gcc:14
gh repo clone code-yeongyu/c11-compiler-zig-omo
```

## Directory layout

```
phase1/
  doom/        # patched id-software/doom
  http2/       # HTTP/2 server (kqueue / epoll / io_uring)
  c11-ref/     # C11 conformance suite
phase2/
  src/         # Zig compiler sources
  tests/
phase3/        # verification scripts (zcc-vs-gcc, gdb/ghidra harnesses)
.github/
  workflows/   # Phase-1, Phase-2, Phase-3 CI
docs/
  team/        # team charter
  decisions/   # ADRs
  playbooks/   # how-to (worktrees, debug, ...)
  reviews/     # PR/phase reviews
```

## Project tracker

[Roadmap issue #1](https://github.com/code-yeongyu/c11-compiler-zig-omo/issues/1)
