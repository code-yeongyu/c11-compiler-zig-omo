# zcc Wave 0 scaffold

`zcc` is the Phase 2 zero-dependency Zig 0.16 C11 compiler.

## Build

```bash
zig build
zig build test
```

## Run

```bash
zig build run -- --help
zig build run -- -E hello.c
```

Wave 0 implements the build system, CLI parsing, and a preprocessor-only token dump stub. Code generation is intentionally not implemented yet.
