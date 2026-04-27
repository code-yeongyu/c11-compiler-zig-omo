# Playbook — Phase-2 Zig Build Discipline

This playbook is the **contract** every `phase2/zcc/` component must satisfy. CI ([`.github/workflows/phase2-zig.yml`](../../.github/workflows/phase2-zig.yml)) calls these targets directly. If your `build.zig` is missing one, your job will fail.

## 1. Target contract

| Target | When CI calls it | What it must do |
|---|---|---|
| `zig build` | every job | Compile the compiler and all supporting tools. |
| `zig build test` | every job | Run the full test suite; **non-zero exit on any failure**. |
| `zig build smoke` | every job | End-to-end smoke test: compile a small C file with `zcc` and run the resulting binary. |
| `zig build fmt` | every job | Verify `zig fmt` produces no changes. |
| `zig build lint` | every job | Run `zig ast-check` on every `.zig` file in `phase2/zcc/src/` and `phase2/zcc/tests/`. |
| `zig build clean` | not in CI, expected for local hygiene | Remove `zig-cache/`, `zig-out/`, and any generated artefacts. |

A `build.zig` **must** define `test`, `smoke`, `fmt`, and `lint`. `clean` is optional but strongly recommended. The `smoke` target must exercise the full pipeline: lex → parse → sema → codegen → assemble → link → run.

## 2. build.zig skeleton

```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // --- compiler executable ---
    const mod = b.createModule(.{
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });
    const exe = b.addExecutable(.{
        .name = "zcc",
        .root_module = mod,
    });
    b.installArtifact(exe);

    // --- unit tests ---
    const test_step = b.step("test", "Run unit tests");
    const test_mod = b.createModule(.{
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });
    const src_tests = b.addTest(.{
        .root_module = test_mod,
    });
    const run_src_tests = b.addRunArtifact(src_tests);
    test_step.dependOn(&run_src_tests.step);

    // --- smoke test ---
    const smoke_step = b.step("smoke", "End-to-end smoke test");
    const smoke_run = b.addRunArtifact(exe);
    smoke_run.addArg("--target");
    smoke_run.addArg(b.fmt("{s}", .{@tagName(target.result.cpu.arch)}));
    smoke_run.addFileArg(b.path("tests/smoke/hello.c"));
    smoke_run.addArg("-o");
    const smoke_out = smoke_run.addOutputFileArg("smoke");
    smoke_step.dependOn(&smoke_run.step);

    // Run the compiled smoke binary
    const runner_mod = b.createModule(.{
        .root_source_file = b.path("tests/smoke/runner.zig"),
        .target = target,
        .optimize = optimize,
    });
    const run_smoke = b.addRunArtifact(b.addExecutable(.{
        .name = "smoke-runner",
        .root_module = runner_mod,
    }));
    run_smoke.addFileArg(smoke_out);
    smoke_step.dependOn(&run_smoke.step);

    // --- fmt check ---
    const fmt_step = b.step("fmt", "Check formatting");
    const fmt = b.addFmt(.{
        .paths = &.{ "src", "tests" },
        .check = true,
    });
    fmt_step.dependOn(&fmt.step);

    // --- lint (ast-check) ---
    const lint_step = b.step("lint", "Run ast-check on all .zig files");
    // NOTE: In a real build.zig, iterate over all .zig files under src/ and tests/
    // using std.fs.walk or a manually-maintained file list. The skeleton below
    // shows the pattern for two representative files; expand to full glob in production.
    const src_files = &.{ "src/main.zig", "src/lexer.zig", "src/parser.zig" };
    for (src_files) |src_file| {
        const lint = b.addSystemCommand(&.{"zig", "ast-check"});
        lint.addFileArg(b.path(src_file));
        lint_step.dependOn(&lint.step);
    }

    // --- clean ---
    const clean_step = b.step("clean", "Remove build artefacts");
    const clean_cache = b.addSystemCommand(&.{"rm", "-rf", "zig-cache"});
    clean_step.dependOn(&clean_cache.step);
    const clean_out = b.addSystemCommand(&.{"rm", "-rf", "zig-out"});
    clean_step.dependOn(&clean_out.step);
}
```

## 3. Directory layout

```
phase2/zcc/
  build.zig           # this playbook applies here
  build.zig.zon       # zero external deps — only std lib
  src/
    main.zig          # driver: CLI parsing, pipeline orchestration
    lexer.zig         # C11 lexer
    parser.zig        # recursive descent parser
    sema.zig          # semantic analysis
    ir.zig            # intermediate representation (optional)
    codegen.zig       # assembly emission
    assembler.zig     # internal assembler (optional)
    linker.zig        # internal linker (optional)
  tests/
    unit/             # per-module unit tests (co-located in src/ via `test` blocks)
    integration/      # multi-module integration tests
    smoke/            # end-to-end: C → zcc → binary → run
    fixtures/         # .c files used by integration and smoke tests
```

Unit tests live **inside** the source files they test, using Zig's `test` blocks. Integration tests live in `tests/integration/` as standalone `.zig` files that import from `src/`. Smoke tests live in `tests/smoke/` as `.c` files plus a `runner.zig` that drives `zcc` and verifies output.

## 4. Test discipline

### Unit tests (co-located)

Every public function in `src/` must have at least one `test` block. The block name should match the function:

```zig
pub fn tokenizeScalar(comptime T: type, buffer: []const T, delimiter: T) std.mem.TokenIterator(T, .scalar) {
    return std.mem.tokenizeScalar(T, buffer, delimiter);
}

test "tokenizeScalar splits on delimiter" {
    // given a buffer with two words separated by a space
    const buffer = "hello world";

    // when we tokenize on space
    var it = tokenizeScalar(u8, buffer, ' ');

    // then we get two tokens
    try std.testing.expectEqualStrings("hello", it.next().?);
    try std.testing.expectEqualStrings("world", it.next().?);
    try std.testing.expect(it.next() == null);
}
```

### Integration tests

Integration tests verify that multiple modules compose correctly. They live in `tests/integration/` and are compiled as separate test binaries in `build.zig`:

```zig
const integration_test = b.addTest(.{
    .root_source_file = b.path("tests/integration/lexer_parser.zig"),
    .target = target,
    .optimize = optimize,
});
```

### Smoke tests

Smoke tests verify the full compiler pipeline. Each smoke test is a `.c` file in `tests/smoke/` that exercises a specific C11 feature. The `runner.zig` compiles it with `zcc`, runs the resulting binary, and checks exit code and stdout:

```zig
test "smoke: hello.c compiles and runs" {
    // given a minimal C program that prints "hello"
    const c_src = "tests/smoke/hello.c";

    // when we compile it with zcc and run the binary
    const output = try runZccAndExec(allocator, c_src);

    // then the output is exactly "hello\n" and exit code is 0
    try std.testing.expectEqualStrings("hello\n", output.stdout);
    try std.testing.expectEqual(0, output.exit_code);
}
```

## 5. CI matrix (`.github/workflows/phase2-zig.yml`)

| Job | Runner | Zig version | Optimize |
|---|---|---|---|
| `zcc / native debug` | ubuntu-latest | 0.16.0 | `-Doptimize=Debug` |
| `zcc / native release` | ubuntu-latest | 0.16.0 | `-Doptimize=ReleaseFast` |
| `zcc / macos debug` | macos-latest | 0.16.0 | `-Doptimize=Debug` |
| `zcc / macos release` | macos-latest | 0.16.0 | `-Doptimize=ReleaseFast` |
| `zcc / docker gcc:14` | ubuntu-latest | 0.16.0 (installed in container) | `-Doptimize=ReleaseFast` |

Host jobs (ubuntu-latest, macos-latest) run the full suite: `zig build`, `zig build test`, `zig build smoke`, `zig build fmt`, and `zig build lint`. The docker job runs a fast subset (`clean`, `test`, `smoke`) because container startup dominates wall-clock time and `fmt`/`lint` are already gated on host jobs. Any non-zero exit fails the job.

## 6. Reproducing CI locally

### macOS host

```bash
zig build -Doptimize=Debug test smoke fmt lint
zig build -Doptimize=ReleaseFast test smoke
```

### Linux host

```bash
zig build -Doptimize=Debug test smoke fmt lint
zig build -Doptimize=ReleaseFast test smoke
```

### Docker (gcc:14 image, for Linux-only validation)

```bash
IMAGE=gcc:14 bash scripts/run-in-docker-phase2.sh zcc clean test smoke
```

## 7. Common failure modes

| Symptom | Cause | Fix |
|---|---|---|
| `zig build` fails with `error: no member named 'addExecutable' in 'Build'` | Using Zig 0.13 or older API | Upgrade to Zig 0.16.0. The `Build` API changed significantly between 0.13 and 0.16. |
| `zig build test` passes but `zig build smoke` fails | Smoke fixture `.c` file uses a C11 feature not yet implemented | Skip that fixture in `build.zig` (conditional `smoke_step.dependOn`) and open a tracking issue. Do not delete the fixture. |
| `zig build fmt` fails | Unformatted `.zig` file | Run `zig fmt src/ tests/` locally and commit the result. |
| `zig build lint` fails | `zig ast-check` reports a syntax error | Fix the syntax error. `ast-check` is stricter than `zig build` for some edge cases. |
| Tests pass on macOS but fail on Linux (or vice versa) | Platform-specific code path (e.g. kqueue vs epoll) | Gate the test with `if (builtin.os.tag != .linux) return error.SkipZigTest;` or add a Linux-specific fixture. |
| `build.zig.zon` has dependencies | Violates the "zero external deps" rule | Remove the dependency and reimplement with std lib. If you believe the dependency is essential, amend ADR-0001 first. |

## 8. Reviewer-quality (`{[reviewer-quality]}`) review checklist

When reviewing a Phase 2 PR, check:

1. `build.zig` defines `test`, `smoke`, `fmt`, and `lint`.
2. All targets succeed locally on **both** macOS-native and `gcc:14` docker.
3. No build artefacts committed (`git ls-files | rg 'zig-cache|zig-out' returns empty`).
4. Tests are real: unit tests **and** an end-to-end smoke test, no mocks unless physically unavoidable, given/when/then in the body.
5. Every cell of the CI matrix above is green.
6. PR body links the relevant Phase 2 tracking issue and includes a Test Plan section per the team charter.
7. `build.zig.zon` has zero dependencies.

If 1–7 pass, comment `reviewer-quality: APPROVE` on the PR. If anything fails, leave a `{[reviewer-quality]}` change-request comment with concrete diffs or log excerpts.

## 9. References

| Source | URL |
|---|---|
| Zig 0.16.0 Release Notes | https://ziglang.org/download/0.16.0/release-notes.html |
| Zig Language Reference | https://ziglang.org/documentation/0.16.0/ |
| `std.Build` API | https://github.com/ziglang/zig/blob/master/lib/std/Build.zig |
| `std.Build.Step.Compile` | https://github.com/ziglang/zig/blob/master/lib/std/Build/Step/Compile.zig |
| ADR-0001 — Toolchain & target matrix | ../../decisions/0001-stack-and-toolchain.md |
