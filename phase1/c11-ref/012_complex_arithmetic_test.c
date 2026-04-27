#include "test_support.h"
#include "012_complex_arithmetic.c"

int main(void)
{
    /* given */
    const int complex_available =
#if defined(__STDC_NO_COMPLEX__)
        0;
#else
        1;
#endif

    /* when */
    const struct complex_arithmetic_result result = complex_arithmetic_run();

    /* then */
    assert(result.supported == complex_available);
    if (complex_available) {
        assert(result.real_part == -5);
        assert(result.imaginary_part == 10);
    }
    C11_REF_OK();
}
