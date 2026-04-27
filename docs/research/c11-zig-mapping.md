# Research — C11 to Zig 0.16 Concept Mapping

This document maps C11 language features to their Zig 0.16 equivalents. It serves two purposes:
1. Guide the `zcc` implementer on how to represent C11 semantics internally.
2. Provide a reference for any C-to-Zig translation or interoperability.

## 1. Type system

### Scalar types

| C11 | Zig 0.16 | Notes |
|---|---|---|
| `char` | `i8` or `u8` | C `char` signedness is implementation-defined. `zcc` should match the target ABI (usually `i8` on x86_64, `u8` on arm64). |
| `short` | `i16` | Always signed in C. |
| `int` | `i32` | C `int` is at least 16 bits; on all our targets it is 32. |
| `long` | `i64` (LP64: macOS/Linux x86_64) or `i32` (LLP64: Windows) | Match target ABI. On macOS/Linux x86_64 (LP64), `long` is 64 bits. On Windows (LLP64), `long` is 32 bits. |
| `long long` | `i64` | Always at least 64 bits. |
| `unsigned X` | `u8`, `u16`, `u32`, `u64` | Zig has no `unsigned` keyword; use the unsigned variant directly. |
| `_Bool` | `bool` | Zig `bool` is 1 bit logically, 1 byte physically. C `_Bool` is 1 byte. |
| `float` | `f32` | IEEE 754 single precision. |
| `double` | `f64` | IEEE 754 double precision. |
| `long double` | `f80` or `f64` or `f128` | Platform-dependent. On x86_64 it is 80-bit extended precision (`f80`). On Darwin arm64 it is `f64` (same as `double`). On Linux arm64 (AAPCS64) it is `f128`. Match the target ABI. |
| `float _Complex` | `std.math.complex.Complex(f32)` | Zig std lib provides complex numbers. |
| `double _Complex` | `std.math.complex.Complex(f64)` | |
| `_Atomic T` | `std.atomic.Value(T)` | For scalar `T`. For structs, use `std.Thread.Mutex` or align to cache line and use atomic builtins. |
| `void` | `void` | Direct match. |
| `size_t` | `usize` | Zig `usize` is the pointer-sized unsigned integer. |
| `ptrdiff_t` | `isize` | Zig `isize` is the pointer-sized signed integer. |
| `intptr_t` / `uintptr_t` | `isize` / `usize` | Direct match. |

### Derived types

| C11 | Zig 0.16 | Notes |
|---|---|---|
| `T*` | `*T` or `*allowzero T` | Zig `*T` is a non-null, non-optional single-item pointer. For C `NULL`, use `?*T`. For zero-as-valid (e.g. x86 real mode), use `*allowzero T`. |
| `const T*` | `*const T` | Direct match. |
| `T[]` (incomplete array) | `[*]T` or `[0]T` | C array parameters decay to pointers; external incomplete arrays carry no length. Use `[*]T` (many-pointer) for general C pointer semantics. Use `[0]T` only for flexible array member ABI layout. |
| `T[N]` | `[N]T` | Fixed-size array. Direct match. |
| `T[N][M]` | `[N][M]T` | Multidimensional array. Direct match. |
| `struct S { ... }` | `const S = struct { ... }` | Zig structs are namespaced. Use `extern struct` for C ABI compatibility. |
| `union U { ... }` | `const U = union { ... }` | Use `extern union` for C ABI. For tagged unions, use `union(enum) { ... }`. |
| `enum E { A, B }` | `const E = enum(c_int) { A, B }` | C enum compatible integer type is implementation/ABI/enumerator dependent. `enum(c_int)` is acceptable for common ABI interop but NOT a complete C11 sema rule. |
| `typedef` | `const` alias or `usingnamespace` | Zig prefers `const MyInt = i32;` over `typedef`. |
| `T (*f)(U, V)` | `*const fn (U, V) T` | Function pointer. Note Zig fn pointers are non-null; use `?*const fn(...)` for nullable. |

### Type qualifiers and attributes

| C11 | Zig 0.16 | Notes |
|---|---|---|
| `const` | `const` | Direct match. |
| `volatile` | `volatile` | Direct match. |
| `restrict` | `noalias` | Zig `noalias` on pointer parameters. |
| `_Alignas(N)` | `align(N)` | Zig `align(N)` on types and variables. |
| `_Alignof(T)` | `@alignOf(T)` | Direct match. |
| `_Static_assert(E, msg)` | `@compileLog` or `comptime assert` | Use `comptime { assert(E); }` for compile-time assertions. |

## 2. Memory and storage

### Storage durations

| C11 | Zig 0.16 | Notes |
|---|---|---|
| Automatic (stack) | Local variable | Direct match. Zig has no implicit stack allocation; locals are on the stack by default. |
| Static (global / file scope) | `var` at module scope | Direct match. |
| Thread-local (`_Thread_local`) | `threadlocal` | Zig `threadlocal var` at module scope. |
| Dynamic (heap) | `allocator.alloc`, `allocator.create` | No implicit heap. Explicit allocator discipline. |

### VLAs (Variable Length Arrays)

C11 VLAs do not map directly to Zig. Zig has no VLA.

```c
// C11 VLA
void foo(int n) {
    int arr[n];  // stack-allocated, size known at runtime
}
```

```zig
// Zig equivalent: allocator or stack slice
fn foo(n: usize) void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    const allocator = gpa.allocator();
    const arr = try allocator.alloc(i32, n);
    defer allocator.free(arr);
}
```

**Implication for `zcc`:** The compiler must lower C VLAs to either:
1. `alloca`-style stack allocation (if the backend supports it).
2. Heap allocation via allocator (safer, but changes semantics for `setjmp`/`longjmp`).

For `zcc` emitting assembly, use the frame pointer + dynamic stack adjustment pattern.

### `alloca`

Zig has no `alloca`. Use `allocator.alloc` or inline assembly for stack pointer manipulation.

## 3. Control flow

| C11 | Zig 0.16 | Notes |
|---|---|---|
| `if (cond)` | `if (cond)` | Direct match. C `if` accepts integers; Zig `if` accepts `bool` only. The compiler must insert `!= 0` for C integer conditions. |
| `switch (x) { case 1: ... }` | `switch (x) { 1 => ..., else => ... }` | Zig `switch` is exhaustive. C `switch` with `default` maps to `else`. |
| `for (init; cond; inc)` | `while (cond) : (inc) { ... }` | Zig `while` supports a continue expression. |
| `while (cond)` | `while (cond)` | Direct match. |
| `do { ... } while (cond)` | `while (true) { ... if (!cond) break; }` | No direct `do-while` in Zig. |
| `break` / `continue` | `break` / `continue` | Direct match. |
| `goto label` | (no equivalent) | Zig has NO arbitrary `goto`. C `goto` must be represented in compiler IR / control-flow graph, NOT mapped to Zig source `goto`. |
| `setjmp` / `longjmp` | `errdefer` / `try` / `catch` | Zig uses error unions, not setjmp. For C compatibility, implement `setjmp`/`longjmp` as inline assembly saving/restoring the frame pointer and stack pointer. |

## 4. Functions

| C11 | Zig 0.16 | Notes |
|---|---|---|
| `T f(U a, V b)` | `fn f(a: U, b: V) T` | Direct match. |
| `void f(void)` | `fn f() void` | C `f(void)` means no parameters. Zig `fn f() void` is the same. |
| `T f(...)` | `fn f(args: anytype) T` or comptime varargs | Zig has no C-style varargs in the type system. Use `std.builtin.VaList` for C interop, or comptime tuples for Zig-native varargs. |
| `inline` | `inline` | Zig `inline fn` forces inlining at the call site. |
| `_Noreturn` | `noreturn` | Zig `noreturn` is a type, not an attribute. A function returning `noreturn` never returns. |
| `static` (file scope) | `pub` vs non-`pub` | Zig non-`pub` declarations are module-private. |
| `extern` | `extern` | Zig `extern` for C ABI compatibility. |
| `__attribute__((packed))` | `packed struct` | Zig `packed struct` has no padding. |
| `__attribute__((aligned(N)))` | `align(N)` | Direct match. |

### Variadic functions

C11 variadic functions are tricky in Zig. For `zcc` implementing C variadics:

1. **Internal representation:** Store the parameter list as a slice of `union { int: i64, float: f64, ptr: *anyopaque }` or similar tagged union.
2. **ABI compliance:** On x86_64 System V, variadic args use the same register/stack convention as fixed args. The callee must know which registers were used for integer vs floating-point args. This is typically handled by the `va_list` type, which in C is a struct tracking register save area and stack args.
3. **Zig implementation:** Use `std.builtin.VaList` when calling C variadic functions from Zig. When `zcc` emits code for a C variadic function, emit the standard ABI prologue that initializes `va_list` from the register save area.

## 5. Concurrency

### Threads

| C11 (`threads.h`) | Zig 0.16 | Notes |
|---|---|---|
| `thrd_create` | `std.Thread.spawn` | Direct match. |
| `thrd_join` | `thread.join()` | Direct match. |
| `thrd_exit` | `return` from thread function | Direct match. |
| `thrd_sleep` | `std.time.sleep` | Nanoseconds in Zig. |
| `thrd_yield` | `std.Thread.yield` | Direct match. |

### Mutexes and condition variables

| C11 | Zig 0.16 | Notes |
|---|---|---|
| `mtx_init` | `std.Thread.Mutex.init` | Zig `Mutex` is a struct, not a pointer. |
| `mtx_lock` | `mutex.lock()` | Direct match. |
| `mtx_unlock` | `mutex.unlock()` | Direct match. |
| `cnd_init` | `std.Thread.Condition.init` | |
| `cnd_wait` | `condition.wait(&mutex)` | |
| `cnd_signal` | `condition.signal()` | |
| `cnd_broadcast` | `condition.broadcast()` | |

### Atomics

| C11 (`stdatomic.h`) | Zig 0.16 | Notes |
|---|---|---|
| `atomic_int` | `std.atomic.Value(i32)` | |
| `atomic_fetch_add` | `value.fetchAdd(delta, ordering)` | |
| `atomic_load` | `value.load(ordering)` | |
| `atomic_store` | `value.store(new, ordering)` | |
| `atomic_compare_exchange_strong` | `value.cmpxchgStrong(expected, desired, success_order, fail_order)` | |
| `memory_order_relaxed` | `.monotonic` | Zig uses `.monotonic` for relaxed. |
| `memory_order_acquire` | `.acquire` | Direct match. |
| `memory_order_release` | `.release` | Direct match. |
| `memory_order_seq_cst` | `.seq_cst` | Direct match. |

## 6. Preprocessor and compile-time features

| C11 | Zig 0.16 | Notes |
|---|---|---|
| `#define` | `const`, `comptime var`, `inline fn` | Zig has no textual preprocessor. Use `comptime` for compile-time computation. |
| `#ifdef` / `#ifndef` | `comptime if` | Zig `comptime if` branches are evaluated at compile time. |
| `#include` | `@import` | Zig modules are imported by path, not by textual inclusion. |
| `_Generic` | `switch (@typeInfo(T))` or `comptime if` | Zig's type-based dispatch is done via `comptime` reflection, not `_Generic`. |
| `static_assert` | `comptime { assert(E); }` | |
| `__FILE__`, `__LINE__` | `@src()` | Zig `@src()` returns a struct with `file`, `fn_name`, `line`, `column`. |
| `__func__` | `@src().fn_name` | |

### `_Generic` mapping

C11 `_Generic` selects an expression based on the type of a controlling expression:

```c
#define abs(x) _Generic((x), \
    int: abs_int, \
    long: abs_long, \
    float: abs_float \
)(x)
```

Zig equivalent using `comptime`:

```zig
fn abs(x: anytype) @TypeOf(x) {
    const T = @TypeOf(x);
    return switch (T) {
        i32 => abs_int(x),
        i64 => abs_long(x),
        f32 => abs_float(x),
        else => @compileError("unsupported type for abs"),
    };
}
```

**Implication for `zcc`:** The preprocessor must expand `_Generic` to the selected expression. The compiler's AST should represent `_Generic` as a type-switch node, lowered to a direct call in sema.

## 7. Standard library mapping

### stdio.h

| C11 | Zig 0.16 | Notes |
|---|---|---|
| `FILE*` | `std.fs.File` | Zig file handles are structs, not opaque pointers. |
| `fopen` | `std.fs.cwd().openFile` | |
| `fread` / `fwrite` | `file.read` / `file.write` | |
| `fseek` / `ftell` | `file.seekTo` / `file.getPos` | |
| `printf` | `std.debug.print` or `std.io.Writer.print` | Zig `print` is type-safe; use `{}` for any type. |
| `sprintf` | `std.fmt.bufPrint` | |
| `malloc` / `free` | `allocator.alloc` / `allocator.free` | Explicit allocator discipline. |
| `exit` | `std.process.exit` | |
| `abort` | `std.process.abort` | |

### string.h

| C11 | Zig 0.16 | Notes |
|---|---|---|
| `memcpy` | `@memcpy` | Zig `@memcpy` is a builtin, not a libc call. |
| `memmove` | `@memmove` | |
| `memset` | `@memset` | |
| `memcmp` | `std.mem.eql` or `std.mem.compare` | |
| `strlen` | `std.mem.len` or `std.mem.sliceTo` | |
| `strcpy` / `strncpy` | `std.mem.copy` | Zig copies are always bounded. |
| `strcat` / `strncat` | `std.mem.concat` | |

### stdlib.h

| C11 | Zig 0.16 | Notes |
|---|---|---|
| `qsort` | `std.sort.block` or `std.sort.pdq` | Zig sort is type-safe and generic. |
| `bsearch` | `std.sort.binarySearch` | |
| `abs` / `labs` / `llabs` | `@abs` | Zig builtin. |
| `div` / `ldiv` | `@divTrunc`, `@divFloor`, `@divExact` | Zig has multiple division modes. |
| `rand` | `std.crypto.random` or `std.Random` | Zig prefers cryptographically secure randomness. |

### math.h

| C11 | Zig 0.16 | Notes |
|---|---|---|
| `sin`, `cos`, `tan` | `std.math.sin`, `std.math.cos`, `std.math.tan` | |
| `sqrt` | `std.math.sqrt` | |
| `pow` | `std.math.pow` | |
| `floor` / `ceil` / `round` | `std.math.floor`, `std.math.ceil`, `std.math.round` | |
| `INFINITY` | `std.math.inf` | |
| `NAN` | `std.math.nan` | |
| `isnan` | `std.math.isNan` | |
| `isinf` | `std.math.isInf` | |

## 8. ABI and calling conventions

| C11 | Zig 0.16 | Notes |
|---|---|---|
| `__attribute__((cdecl))` | `.C` calling convention | Zig `callconv(.C)` for C ABI. |
| `__attribute__((stdcall))` | `.Stdcall` | Windows only. |
| `__attribute__((fastcall))` | `.Fastcall` | |
| `__attribute__((thiscall))` | `.Thiscall` | C++ only, but relevant for C++ interop. |
| `__attribute__((sysv_abi))` | `.C` on x86_64 Linux/macOS | System V AMD64 ABI is the default `.C` on those platforms. |
| `__attribute__((ms_abi))` | `.C` on x86_64 Windows | Microsoft x64 ABI is the default `.C` on Windows. |
| `__attribute__((vector_size(N)))` | `@Vector(N, T)` | Zig SIMD vectors. |
| `__attribute__((noreturn))` | `noreturn` return type | |

## 9. Edge cases and warnings

### Signed integer overflow

C11 signed integer overflow is undefined behavior. Zig signed integer overflow TRAPS in safe modes (Debug, ReleaseSafe). Wrapping requires the `+%` operators or explicit overflow builtins (`@addWithOverflow`, `@subWithOverflow`, `@mulWithOverflow`).

**Implication for `zcc`:** When compiling C11 with `zcc`, the default should match C semantics (UB on signed overflow). In debug builds, `zcc` may optionally emit overflow checks. Do not assume Zig wrapping behavior applies to C code.

### Pointer arithmetic

C11 pointer arithmetic is only defined within arrays (or one past the end). Zig pointer arithmetic is more restrictive: `ptr + n` requires `ptr` to be a multi-item pointer `[*]T`, not a single-item pointer `*T`.

**Implication for `zcc`:** The compiler's internal representation should use `[*]T` for all C pointers that may participate in arithmetic.

### Integer promotion and usual arithmetic conversions

C11 has complex rules for integer promotion (`char` → `int`, etc.) and usual arithmetic conversions (balancing `int` and `unsigned int`). Zig has no implicit integer promotion: `u8 + u8` produces `u8`, not `u16` or `u32`.

**Implication for `zcc`:** The sema phase must explicitly insert cast nodes for all C implicit promotions. The IR should represent these casts explicitly.

### `union` punning

C11 allows reading a `union` member other than the one last written (type punning). Zig `union` (non-`extern`, non-`packed`) has active field safety checks. Use `extern union` or `packed union` for C-compatible punning.

**Implication for `zcc`:** All C `union` types should map to `extern union` in any Zig interop layer. The compiler's internal representation should not enforce active field tracking.

### Flexible array members

C99/C11 flexible array member:

```c
struct Buffer {
    size_t len;
    char data[];  // flexible array member
};
```

Zig has no direct equivalent. Use a slice:

```zig
const Buffer = extern struct {
    len: usize,
    data: [0]u8,  // zero-length array, then access via pointer arithmetic
};
```

Or represent as:

```zig
const Buffer = struct {
    len: usize,
    data: []u8,  // ptr+len slice
};
```

**Implication for `zcc`:** For ABI compatibility, use `[0]T` at the end of an `extern struct`. For internal representation, use a slice.

## 10. References

- C11 Standard (N1570): https://open-std.org/jtc1/sc22/WG14/www/docs/n1570.pdf
- Zig 0.16 Language Reference: https://ziglang.org/documentation/0.16.0/
- Zig `std.atomic.Value`: https://github.com/ziglang/zig/blob/master/lib/std/atomic/Value.zig
- Zig `std.Thread`: https://github.com/ziglang/zig/blob/master/lib/std/Thread.zig
- System V AMD64 ABI: https://gitlab.com/x86-psABIs/x86-64-ABI
- arocc (C compiler in Zig): https://github.com/Vexu/arocc
