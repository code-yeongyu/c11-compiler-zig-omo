#include "test_support.h"
#include "015_static_assert_runtime_mix.c"

int main(void)
{
    /* given */
    const int expected = 0xC011 + 3;

    /* when */
    const int actual = static_assert_runtime_mix_run();

    /* then */
    assert(actual == expected);
    C11_REF_OK();
}
