#include "test_support.h"
#include "017_integer_promotions.c"

int main(void)
{
    /* given */
    const int character_constant_is_int = 2;

    /* when */
    const struct integer_promotions_result result = integer_promotions_run();

    /* then */
    assert(result.unsigned_char_sum == UCHAR_MAX + 1);
    assert(result.signed_char_negative == 1);
    assert(result.bitfield_promoted == 8);
    assert(result.character_constant_type == character_constant_is_int);
    C11_REF_OK();
}
