#if !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
#endif

struct atomic_specifier_result {
    int supported;
    int compare_exchange_succeeded;
    unsigned stored_value;
};

struct atomic_specifier_result atomic_specifier_run(void)
{
#if defined(__STDC_NO_ATOMICS__)
    return (struct atomic_specifier_result){ .supported = 0, .compare_exchange_succeeded = 0, .stored_value = 0u };
#else
    _Atomic unsigned slot;
    unsigned expected = 11u;
    const unsigned replacement = 99u;
    atomic_init(&slot, expected);
    const int exchanged = atomic_compare_exchange_strong_explicit(
        &slot, &expected, replacement, memory_order_acq_rel, memory_order_acquire);
    const unsigned loaded = atomic_load_explicit(&slot, memory_order_relaxed);

    return (struct atomic_specifier_result){
        .supported = 1,
        .compare_exchange_succeeded = exchanged,
        .stored_value = loaded,
    };
#endif
}
