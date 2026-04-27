# Research — Zig 0.16 Async Patterns for Compiler Workloads

This document maps Zig 0.16 concurrency primitives to the specific needs of a C11 compiler. It is a living reference: update it when new Zig versions land or when `zcc` adopts a pattern not listed here.

## 1. The `std.Io` interface (replaces old `async`/`await`)

Zig 0.16 removed the `async` and `await` keywords. Asynchrony is now expressed through the `std.Io` interface, passed like `Allocator`.

### Core primitives

| Primitive | Returns | Use when |
|---|---|---|
| `io.async(func, args)` | `Future(T)` | You want to decouple the call from the return. The backend may or may not run it concurrently. |
| `io.concurrent(func, args)` | `Future(T)` | You require concurrency. Fails with `error.ConcurrencyUnavailable` if the backend cannot oversubscribe. |
| `future.await(io)` | `T` or `!T` | Block until the future resolves. Idempotent. |
| `future.cancel(io)` | `Result` (same type as `await`) | Cancel the future. Returns the same result type the future would have produced from `await`; callers must handle it. Idempotent. |

### Example: async file read during lexing

```zig
fn lexFile(io: std.Io, allocator: std.mem.Allocator, path: []const u8) !TokenList {
    // given a source file path
    // when we read it asynchronously
    var future = io.async(readSourceFile, .{ allocator, path });
    defer future.cancel(io) catch {};
    const source = try future.await(io);
    defer allocator.free(source);

    // then we tokenize the contents
    return try tokenize(allocator, source);
}

fn readSourceFile(allocator: std.mem.Allocator, path: []const u8) ![]u8 {
    const file = try std.fs.cwd().openFile(path, .{});
    defer file.close();
    return try file.readToEndAlloc(allocator, 1024 * 1024 * 10);
}
```

### Backends

| Backend | What it does | When to use |
|---|---|---|
| `std.Io.Threaded` | Thread pool with work stealing | Default for CPU-bound compiler work. |
| `std.Io.Evented` | io_uring (Linux), GCD (macOS) | Experimental. Use for I/O-bound phases (reading many TUs, writing object files). |

**Implication for `zcc`:** Pass `std.Io` through the driver. Do not design around old `async`/`await` keywords. The driver should create one `std.Io.Threaded` instance at startup and pass it to every compilation phase that can benefit from parallelism.

## 2. Thread pools (`std.Io.Threaded`)

`std.Io.Threaded` is the CPU-bound parallelism backend in Zig 0.16. It provides a thread pool with work stealing, accessed through the `std.Io` interface.

### Pattern: parallel compilation units

```zig
fn compileAll(gpa: std.mem.Allocator, io: std.Io, tus: []const TranslationUnit) !void {
    // given a list of translation units
    // when we create a group and spawn one concurrent task per TU
    var group: std.Io.Group = .init;
    for (tus) |tu| {
        try group.concurrent(io, compileTu, .{ gpa, io, tu });
    }

    // then we wait for all tasks to complete
    try group.await(io);
}
```

### Key methods

| Method | Behavior |
|---|---|
| `group.concurrent(io, func, args)` | Add a concurrent task to the group. Fails with `error.ConcurrencyUnavailable` if backend cannot oversubscribe. |
| `group.await(io)` | Block until every task in the group resolves. |
| `io.async(func, args)` | Decouple call from return; may or may not run concurrently. |

**Implication for `zcc`:** Use `group.concurrent` for per-TU compilation. Each TU gets its own `ArenaAllocator`. The main thread calls `group.await(io)` and then proceeds to the link step.

## 3. Groups (`std.Io.Group`)

`std.Io.Group` is the canonical wait-group API in Zig 0.16. It synchronizes batches of concurrent tasks.

### Pattern: phased pipeline

```zig
fn pipeline(gpa: std.mem.Allocator, io: std.Io, tus: []const TranslationUnit) !void {
    // given translation units
    var lex_group: std.Io.Group = .init;
    var parse_group: std.Io.Group = .init;

    // when we lex all TUs in parallel
    for (tus) |tu| {
        try lex_group.concurrent(io, lexTu, .{ gpa, tu });
    }
    try lex_group.await(io);

    // then we parse all TUs in parallel
    for (tus) |tu| {
        try parse_group.concurrent(io, parseTu, .{ gpa, tu });
    }
    try parse_group.await(io);
}
```

**Implication for `zcc`:** If you want a phased pipeline (all lexing done before any parsing starts), use separate `Io.Group` instances per phase. If you want a streaming pipeline (parse starts as soon as lexing finishes for one TU), use a mutex-protected queue or `std.DoublyLinkedList` + `Mutex`.

## 4. Lock-free primitives (`std.atomic.Value`)

`std.atomic.Value(T)` replaced `std.atomic.Atomic` in 0.16. It is a thin wrapper over atomic builtins.

### Pattern: diagnostic counter

```zig
var error_count = std.atomic.Value(usize).init(0);

fn reportError() void {
    _ = error_count.fetchAdd(1, .monotonic);
}

fn hasErrors() bool {
    return error_count.load(.acquire) > 0;
}
```

### Pattern: global symbol table versioning

```zig
var symtab_version = std.atomic.Value(u32).init(0);

fn enterSymbol(symtab: *SymbolTable, sym: Symbol) void {
    const version = symtab_version.fetchAdd(1, .seq_cst);
    sym.version = version;
    // ... insert into symtab with mutex or lock-free structure
}
```

**Implication for `zcc`:** Use `std.atomic.Value` for:
- Error/warning counters (monotonic is sufficient).
- Unique ID generation (seq_cst to avoid collisions).
- Global symbol table versioning (seq_cst).

Do not use it for complex data structures. For MPMC queues, use `std.DoublyLinkedList` + `Mutex`. (Zero-external-deps mandate: do not add third-party queue libraries.)

## 5. Memory arenas (`std.heap.ArenaAllocator`)

In 0.16, `heap.ArenaAllocator` is thread-safe and lock-free. This is ideal for per-TU allocation.

### Pattern: one arena per TU

```zig
fn compileTu(gpa: std.mem.Allocator, io: std.Io, tu: TranslationUnit) !ObjectFile {
    // given a translation unit
    var arena = std.heap.ArenaAllocator.init(gpa);
    defer arena.deinit();
    const a = arena.allocator();

    // when we allocate all AST/IR data in the arena
    const tokens = try lex(a, tu.source);
    const ast = try parse(a, tokens);
    const ir = try lower(a, ast);
    const obj = try codegen(a, ir);

    // then we return the object file; the arena frees everything on defer
    return obj;
}
```

**Implication for `zcc`:** Every TU gets its own `ArenaAllocator`. All AST nodes, IR instructions, and local symbol tables live in the arena. The object file (or assembly text) is the only thing that escapes the arena. Deinit the arena after the TU is fully compiled.

## 6. Multi-process workers (`std.process.Child`)

For sandboxing or invoking external tools (assembler, linker), use `std.process`.

### Pattern: invoke system assembler

```zig
fn assemble(gpa: std.mem.Allocator, io: std.Io, asm_path: []const u8, obj_path: []const u8) !void {
    // given an assembly text file
    const result = try std.process.run(gpa, io, .{
        .argv = &.{ "as", "-o", obj_path, asm_path },
    });
    defer gpa.free(result.stdout);
    defer gpa.free(result.stderr);

    // then we check the exit code
    switch (result.term) {
        .exited => |code| if (code != 0) {
            std.log.err("assembler failed: {s}", .{result.stderr});
            return error.AssembleFailed;
        },
        else => return error.AssembleFailed,
    }
}
```

**Implication for `zcc`:** Use `std.process.run` for simple synchronous invocations (assembler, linker) — it blocks until the child exits. For non-blocking execution or streaming stdout/stderr, use `std.process.spawn` with `.stdout = .pipe` / `.stderr = .pipe`, then read via `std.Io.File.reader(io, &buffer)` and `Reader.allocRemaining`. Consult the Zig 0.16 stdlib (`std.Io.File.zig`, `std.process.zig`) for exact signatures — the pipe-reader API is detailed and out of scope for this guide.

## 7. Combining patterns: a full driver sketch

```zig
pub fn main() !void {
    var gpa = std.heap.DebugAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    // given CLI arguments
    const args = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, args);

    // when we initialize the I/O backend
    var threaded = std.Io.Threaded.init(allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    // compile each TU in parallel
    var group: std.Io.Group = .init;
    for (args[1..]) |tu_path| {
        try group.concurrent(io, compileTuPipeline, .{ allocator, io, tu_path });
    }
    try group.await(io);

    // then link all object files
    try linkObjects(allocator, io, &obj_files);
}
```

## 8. What not to use

| Removed / Deprecated | Replacement | Why |
|---|---|---|
| `std.atomic.Atomic` | `std.atomic.Value` | Cleaner API, same builtins underneath. |
| `std.atomic.Queue` / `std.atomic.Stack` | `std.DoublyLinkedList` + `Mutex` | Removed from std lib. Do not add third-party replacements. |
| `heap.ThreadSafeAllocator` | `heap.ArenaAllocator` (now thread-safe) | Simplified API. |
| `@cImport` | `addTranslateC` in build system | C translation moved to build time. |
| `@Type` | `@Int`, `@Struct`, `@Union`, `@Enum`, `@Pointer`, `@Fn`, `@Tuple`, `@EnumLiteral` | Finer-grained builtins. |

## 9. References

- Zig 0.16.0 Release Notes: https://ziglang.org/download/0.16.0/release-notes.html
- Zig Language Reference: https://ziglang.org/documentation/0.16.0/
- `std.Io.Threaded`: https://github.com/ziglang/zig/blob/master/lib/std/Io/Threaded.zig
- `std.process`: https://github.com/ziglang/zig/blob/master/lib/std/process.zig
- `std.MultiArrayList`: https://github.com/ziglang/zig/blob/master/lib/std/multi_array_list.zig
- Zig's New Async I/O (Andrew Kelley): https://andrewkelley.me/post/zig-new-async-io-text-version.html
