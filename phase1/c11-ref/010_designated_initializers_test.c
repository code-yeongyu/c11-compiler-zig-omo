#include "test_support.h"
#include "010_designated_initializers.c"

int main(void)
{
    /* given */
    const int expected_first_sum = 20;

    /* when */
    const struct designated_initializers_result result = designated_initializers_run();

    /* then */
    assert(result.first_sum == expected_first_sum);
    assert(result.sparse_value == 5);
    assert(result.nested_value == 23);
    C11_REF_OK();
}
