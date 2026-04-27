#include "test_support.h"
#include "016_optional_feature_macros.c"

int main(void)
{
    /* given */
    const int always_boolean = 1;

    /* when */
    const struct optional_feature_macros_result result = optional_feature_macros_run();

    /* then */
    assert((result.atomics == 0 || result.atomics == 1) == always_boolean);
    assert((result.threads == 0 || result.threads == 1) == always_boolean);
    assert((result.complex_numbers == 0 || result.complex_numbers == 1) == always_boolean);
    assert((result.vla == 0 || result.vla == 1) == always_boolean);
    C11_REF_OK();
}
