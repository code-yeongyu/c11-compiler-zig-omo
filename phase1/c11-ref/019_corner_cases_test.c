#include "test_support.h"
#include "019_corner_cases.c"

int main(void)
{
    /* given */
    const int character_constant_is_int = 1;

    /* when */
    const struct corner_cases_result result = corner_cases_run();

    /* then */
    assert(result.char_constant_kind == character_constant_is_int);
    assert(result.func_name_nonempty == 1);
    assert(result.aligned_offset % 16u == 0u);
    assert(result.typedef_value == 33u);
    C11_REF_OK();
}
