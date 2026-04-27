#include "test_support.h"
#include "008_anonymous_struct_union.c"

int main(void)
{
    /* given */
    const long expected_overlay = 44L;

    /* when */
    const struct anonymous_struct_union_result result = anonymous_struct_union_run();

    /* then */
    assert(result.field_sum == 13);
    assert(result.overlay_value == expected_overlay);
    C11_REF_OK();
}
