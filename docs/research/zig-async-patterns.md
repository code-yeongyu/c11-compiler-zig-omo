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
| `future.cancel(io)` | `void` or `!void` | Cancel the future. Idempotent. |

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
| `std.Io.Evented` | io_uring (Linux), kqueue (macOS), GCD (macOS) | Experimental. Use for I/O-bound phases (reading many TUs, writing object files). |

**Implication for `zcc`:** Pass `std.Io` through the driver. Do not design around old `async`/`await` keywords. The driver should create one `std.Io.Threaded` instance at startup and pass it to every compilation phase that can benefit from parallelism.

## 2. Thread pools (`std.Io.Threaded`)

`std.Io.Threaded` is the CPU-bound parallelism backend in Zig 0.16. It provides a thread pool with work stealing, accessed through the `std.Io` interface.

### Pattern: parallel compilation units

```zig
fn compileAll(gpa: std.mem.Allocator, io: std.Io, tus: []const TranslationUnit) !void {
    // given a list of translation units
    // when we create a group and spawn one concurrent task per TU
    var group = std.Io.Group.init(gpa);
    defer group.deinit();
    for (tus) |tu| {
        const future = try io.concurrent(compileTu, .{ gpa, io, tu });
        group.add(future);
    }

    // then we wait for all tasks to complete
    try group.awaitAll(io);
}
```

### Key methods

| Method | Behavior |
|---|---|
| `io.concurrent(func, args)` | Require concurrency; returns `!std.Io.Future(T)`. Fails if backend cannot oversubscribe. |
| `group.add(future)` | Add a future to the group for batch awaiting. |
| `group.awaitAll(io)` | Block until every future in the group resolves. |
| `io.async(func, args)` | Decouple call from return; may or may not run concurrently. |

**Implication for `zcc`:** Use `io.concurrent` for per-TU compilation. Each TU gets its own `ArenaAllocator`. The main thread calls `group.awaitAll(io)` and then proceeds to the link step.

## 3. Groups (`std.Io.Group`)

`std.Io.Group` synchronizes batches of futures. It replaces the old `std.Thread.WaitGroup` pattern in 0.16.

### Pattern: phased pipeline

```zig
fn pipeline(gpa: std.mem.Allocator, io: std.Io, tus: []const TranslationUnit) !void {
    // given translation units
    var lex_group = std.Io.Group.init(gpa);
    defer lex_group.deinit();
    var parse_group = std.Io.Group.init(gpa);
    defer parse_group.deinit();

    // when we lex all TUs in parallel
    for (tus) |tu| {
        const future = try io.concurrent(lexTu, .{ gpa, tu });
        lex_group.add(future);
    }
    try lex_group.awaitAll(io);

    // then we parse all TUs in parallel
    for (tus) |tu| {
        const future = try io.concurrent(parseTu, .{ gpa, tu });
        parse_group.add(future);
    }
    try parse_group.awaitAll(io);
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

### Pattern: lower-level pipe control

```zig
fn runWithPipes(gpa: std.mem.Allocator, io: std.Io, argv: []const []const u8) !ChildOutput {
    var child = try std.process.spawn(io, .{
        .argv = argv,
        .stdout = .pipe,
        .stderr = .pipe,
    });

    const stdout = try child.stdout.reader().readAllAlloc(gpa, 1024 * 1024);
    errdefer gpa.free(stdout);
    const stderr = try child.stderr.reader().readAllAlloc(gpa, 1024 * 1024);
    errdefer gpa.free(stderr);

    const term = try child.wait(io);
    return .{ .term = term, .stdout = stdout, .stderr = stderr };
}
```

**Implication for `zcc`:** Use `std.process.run` for simple fire-and-forget invocations (assembler, linker). Use the lower-level `std.process.spawn` API when you need to capture stdout/stderr or stream data to the child.

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
    var threaded = try std.Io.Threaded.init(allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    // compile each TU in parallel
    var group = std.Io.Group.init(allocator);
    defer group.deinit();
    for (args[1..]) |tu_path| {
        const future = try io.concurrent(compileTuPipeline, .{ allocator, io, tu_path });
        group.add(future);
    }
    try group.awaitAll(io);

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
