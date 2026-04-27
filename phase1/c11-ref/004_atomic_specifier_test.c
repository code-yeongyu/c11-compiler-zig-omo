#include "test_support.h"
#include "004_atomic_specifier.c"

int main(void)
{
    /* given */
    const int atomics_available =
#if defined(__STDC_NO_ATOMICS__)
        0;
#else
        1;
#endif

    /* when */
    const struct atomic_specifier_result result = atomic_specifier_run();

    /* then */
    assert(result.supported == atomics_available);
    if (atomics_available) {
        assert(result.compare_exchange_succeeded == 1);
        assert(result.stored_value == 99u);
    }
    C11_REF_OK();
}
