# Playbook — C11 features the reference suite must cover

Source: librarian research bg_028a8eef, distilled here for both `phase1/c11-ref/` and the Phase-2 compiler's coverage matrix.

## New C11 features (vs C99) and where each is mandatory

| # | Feature | Mandatory | N1570 § |
|---|---|---|---|
| 1 | `_Generic` selection | yes | 6.5.1.1 |
| 2 | `_Alignas` | yes | 6.7.5 |
| 3 | `_Alignof` | yes | 6.5.3.4 |
| 4 | `_Atomic` qualifier / specifier | optional | 6.7.2.4, 6.7.3, 7.17 |
| 5 | `_Thread_local` | optional | 6.2.4, 6.7.1, 7.26 |
| 6 | `_Noreturn` | yes | 6.7.4, 7.23 |
| 7 | `_Static_assert` | yes | 6.7.10 |
| 8 | Anonymous struct/union members | yes | 6.7.2.1 |
| 9 | `gets` removed, `gets_s` added (Annex K) | yes | 7.21.7.9 (gone) |
| 10 | `fopen "x"` mode | yes | 7.21.5.3 |
| 11 | `aligned_alloc` | yes | 7.22.3.1 |
| 12 | `quick_exit`, `at_quick_exit` | yes | 7.22.4.7 |
| 13 | `timespec_get` | yes | 7.27.2.5 |
| 14 | `char16_t`, `char32_t`, `u`/`U`/`u8` literals | yes | 6.4.4.4, 6.4.5, 7.28 |
| 15 | `<stdalign.h>` | yes | 7.15 |
| 16 | `<stdnoreturn.h>` | yes | 7.23 |
| 17 | `<stdatomic.h>` | optional | 7.17 |
| 18 | `<threads.h>` | optional | 7.26 |
| 19 | `<uchar.h>` | yes | 7.28 |
| 20 | Annex K bounds-checking | optional | Annex K |
| 21 | Annex L analyzability | optional | Annex L |
| 22 | `max_align_t` | yes | 7.19.2 |
| 23 | Memory model / sequencing | optional | 5.1.2.4 |
| 24 | `typedef` redefinition (same type) | yes | 6.7.8 |

## Optional-feature macros

A conforming implementation predefines these *before* any header is included:

| Macro | Meaning |
|---|---|
| `__STDC_NO_ATOMICS__` | no `_Atomic`, no `<stdatomic.h>` |
| `__STDC_NO_THREADS__` | no `<threads.h>` |
| `__STDC_NO_COMPLEX__` | no `_Complex`, no `<complex.h>` |
| `__STDC_NO_VLA__` | no variable-length arrays |
| `__STDC_LIB_EXT1__` | Annex K supported |
| `__STDC_ANALYZABLE__` | Annex L supported |
| `__STDC_IEC_559__` | Annex F (IEC 60559 FP) |
| `__STDC_IEC_559_COMPLEX__` | Annex G |

The C11 reference suite must guard each optional feature with the corresponding `#ifdef`/`#ifndef`.

## Corner cases that break naive compilers (must be exercised)

- `_Generic` matches *post-lvalue-conversion, pre-promotion* type. Character constants (e.g. `'a'`) have type `int`, not `char`.
- `_Generic` strips qualifiers (`const`/`volatile`/`restrict`) before matching (§6.5.1.1p2).
- `_Generic` with no `default` and no match → constraint violation, must diagnose at translation time.
- `_Atomic _Bool` is *not* the same as `atomic_flag`. Only `atomic_flag` is guaranteed lock-free.
- `_Atomic` may not appear on parameters (§6.7.3p3).
- `_Atomic(struct{char val[16];})` size/alignment varies between gcc and clang.
- `_Alignas` cannot appear on bit-fields, functions, or parameters (§6.7.5p2).
- `_Alignas(0)` is allowed and means "natural alignment" (no effect).
- VLAs are forbidden as struct members (§6.7.6.2).
- `__func__` is a *predefined identifier in block scope*, not a macro; not legal at file scope.
- `gets()` is removed in C11 — strict conformance must reject it.

## Suggested file layout for `phase1/c11-ref/`

```
phase1/c11-ref/
├── Makefile
├── README.md
├── 001_generic_selection.c
├── 001_generic_selection_test.c
├── 002_alignas_alignof.c
├── 002_alignas_alignof_test.c
├── 003_atomic_qualifier.c
├── 003_atomic_qualifier_test.c
├── 004_atomic_specifier.c
├── 004_atomic_specifier_test.c
├── 005_thread_local.c
├── 005_thread_local_test.c
├── 006_noreturn.c
├── 006_noreturn_test.c
├── 007_static_assert.c
├── 008_anonymous_struct_union.c
├── 008_anonymous_struct_union_test.c
├── 009_unicode_literals.c
├── 009_unicode_literals_test.c
├── 010_designated_initializers.c
├── 010_designated_initializers_test.c
├── 011_compound_literals.c
├── 011_compound_literals_test.c
├── 012_complex_arithmetic.c
├── 012_complex_arithmetic_test.c
├── 013_vla.c
├── 013_vla_test.c
├── 014_aligned_alloc.c
├── 014_aligned_alloc_test.c
├── 015_static_assert_runtime_mix.c
├── 015_static_assert_runtime_mix_test.c
├── 016_optional_feature_macros.c
├── 017_integer_promotions.c
├── 017_integer_promotions_test.c
├── 018_usual_arithmetic_conversions.c
├── 018_usual_arithmetic_conversions_test.c
└── 019_corner_cases.c
```

Every `*_test.c` has a `main` that returns 0 on success and prints `OK\n`.

## Key references

- N1570 (the publicly available C11 working draft) — sections cited per row above.
- chibicc (`rui314/chibicc`) tests: `test/generic.c`, `test/atomic.c`, `test/alignof.c`, `test/tls.c`, `test/compat.c`.
- c-testsuite (`c-testsuite/c-testsuite`): `tests/single-exec/00219.c`.
- Clang sema tests: `clang/test/Sema/generic-selection.c`, `clang/test/Sema/atomic.c`.
