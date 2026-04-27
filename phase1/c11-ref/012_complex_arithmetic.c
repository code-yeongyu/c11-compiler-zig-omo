#if !defined(__STDC_NO_COMPLEX__)
#include <complex.h>
#endif

struct complex_arithmetic_result {
    int supported;
    int real_part;
    int imaginary_part;
};

struct complex_arithmetic_result complex_arithmetic_run(void)
{
#if defined(__STDC_NO_COMPLEX__)
    return (struct complex_arithmetic_result){ .supported = 0, .real_part = 0, .imaginary_part = 0 };
#else
    const double complex value = (3.0 + 4.0 * I) * (1.0 + 2.0 * I);
    return (struct complex_arithmetic_result){
        .supported = 1,
        .real_part = (int)creal(value),
        .imaginary_part = (int)cimag(value),
    };
#endif
}
