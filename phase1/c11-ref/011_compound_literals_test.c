#include "test_support.h"
#include "011_compound_literals.c"

int main(void)
{
    /* given */
    const int expected_pointer_sum = 42;

    /* when */
    const struct compound_literals_result result = compound_literals_run();

    /* then */
    assert(result.scalar_sum == 10);
    assert(result.array_sum == 8);
    assert(result.pointer_sum == expected_pointer_sum);
    C11_REF_OK();
}
