#include <limits.h>

struct integer_promotions_result {
    int unsigned_char_sum;
    int signed_char_negative;
    int bitfield_promoted;
    int character_constant_type;
};

struct integer_promotions_bits {
    unsigned int small : 3;
};

#define INTEGER_PROMOTION_KIND(value) \
    _Generic((value), \
        char: 1, \
        int: 2, \
        unsigned int: 3, \
        default: 4 \
    )

struct integer_promotions_result integer_promotions_run(void)
{
    unsigned char a = UCHAR_MAX;
    signed char b = -2;
    struct integer_promotions_bits bits = { .small = 7u };

    return (struct integer_promotions_result){
        .unsigned_char_sum = a + 1,
        .signed_char_negative = b < 0,
        .bitfield_promoted = bits.small + 1,
        .character_constant_type = INTEGER_PROMOTION_KIND('x'),
    };
}
