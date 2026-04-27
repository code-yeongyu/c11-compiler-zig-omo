#include "test_support.h"
#include "014_aligned_alloc.c"

int main(void)
{
    /* given */
    const int expected_success = 1;

    /* when */
    const struct aligned_alloc_result result = aligned_alloc_run();

    /* then */
    assert(result.allocated == expected_success);
    assert(result.aligned == expected_success);
    C11_REF_OK();
}
