# ADR-0001 — Toolchain & target matrix

## Status
Accepted. 2026-04-27.

## Context
We need three deliverables that must all interoperate (Phase 1 reference C, Phase 2 Zig C11 compiler, Phase 3 verification). Picking a stable tool matrix early avoids churn.

## Decision

| Concern | Choice |
|---|---|
| Host A (developer machine) | macOS arm64 (Tahoe), Apple LLVM clang + Homebrew gcc |
| Host B (CI / Linux side) | `gcc:13` and `gcc:14` Docker images on OrbStack, plus `ubuntu-latest` GitHub runner |
| Zig version | **0.16.0** (Homebrew bottle) — pinned in CI via `mlugg/setup-zig@v1` |
| Debugging | gdb 17.1 (Linux container), lldb (macOS host), Ghidra 12.0.4 (static analysis) |
| HTTP/2 server I/O | macOS → kqueue (auto-detected); Linux → choose **one** of epoll / io_uring at build time via `make IO=epoll` / `make IO=io_uring` |
| C11 reference | Build with **both** `gcc -std=c11 -pedantic -Wall -Wextra` and `clang -std=c11`, on host **and** in Linux container |
| zcc deps | **zero** external libraries, std lib only |
| zcc parallelism | std-lib `Thread`, `Thread.Pool`, `Thread.Channel`-style queues, async I/O via std lib, multi-process spawning for parallel compilation units |
| Output target | x86_64 + arm64, ELF (Linux) and Mach-O (macOS); we emit textual assembly and hand to a system assembler/linker for the final binary |
| Verification | Differential test against host gcc/clang; run resulting binaries inside `gcc:14` container with `gdb` and on host with `lldb`; static analysis with `ghidra-analyzeHeadless` |

## Consequences

- We do not ship a private linker. We rely on the system `as`/`ld` to assemble & link, since "produce assembly" is the explicit goal.
- All commands and CI must support the matrix `{macos-host, ubuntu-host, gcc:13-docker, gcc:14-docker} × {-O0, -O2}` for Phase-3 verification.
- Anyone proposing a new dependency must amend this ADR first.
