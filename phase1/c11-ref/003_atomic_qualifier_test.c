#include "test_support.h"
#include "003_atomic_qualifier.c"

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
    const struct atomic_qualifier_result result = atomic_qualifier_run();

    /* then */
    assert(result.supported == atomics_available);
    if (atomics_available) {
        assert(result.final_value == 10);
        assert(result.flag_was_clear == 1);
    }
    C11_REF_OK();
}
