#include <stddef.h>

#if !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
#endif

struct atomic_qualifier_result {
    int supported;
    int final_value;
    int flag_was_clear;
};

struct atomic_qualifier_result atomic_qualifier_run(void)
{
#if defined(__STDC_NO_ATOMICS__)
    return (struct atomic_qualifier_result){ .supported = 0, .final_value = 0, .flag_was_clear = 0 };
#else
    _Atomic int counter = ATOMIC_VAR_INIT(4);
    atomic_flag flag = ATOMIC_FLAG_INIT;
    const int was_clear = !atomic_flag_test_and_set_explicit(&flag, memory_order_acquire);
    atomic_fetch_add_explicit(&counter, 6, memory_order_relaxed);
    atomic_flag_clear_explicit(&flag, memory_order_release);
    return (struct atomic_qualifier_result){
        .supported = 1,
        .final_value = atomic_load_explicit(&counter, memory_order_relaxed),
        .flag_was_clear = was_clear,
    };
#endif
}
