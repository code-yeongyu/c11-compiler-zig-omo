#include "test_support.h"
#include "007_static_assert.c"

int main(void)
{
    /* given */
    const int expected = 77;

    /* when */
    const int actual = static_assert_runtime_value();

    /* then */
    assert(actual == expected);
    C11_REF_OK();
}
