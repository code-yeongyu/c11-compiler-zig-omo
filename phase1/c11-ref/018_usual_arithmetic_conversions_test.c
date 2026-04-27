#include "test_support.h"
#include "018_usual_arithmetic_conversions.c"

int main(void)
{
    /* given */
    const int double_kind = 4;
    const int long_double_kind = 5;

    /* when */
    const struct usual_arithmetic_conversions_result result = usual_arithmetic_conversions_run();

    /* then */
    assert(result.float_rank_selected == double_kind);
    assert(result.unsigned_wrap_greater == 1);
    assert(result.long_double_rank_selected == long_double_kind);
    C11_REF_OK();
}
