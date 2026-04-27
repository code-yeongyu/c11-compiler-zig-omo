struct usual_arithmetic_conversions_result {
    int float_rank_selected;
    int unsigned_wrap_greater;
    int long_double_rank_selected;
};

#define USUAL_CONVERSION_KIND(value) \
    _Generic((value), \
        int: 1, \
        unsigned int: 2, \
        long: 3, \
        double: 4, \
        long double: 5, \
        default: 6 \
    )

struct usual_arithmetic_conversions_result usual_arithmetic_conversions_run(void)
{
    unsigned int unsigned_value = 1u;
    int signed_negative = -2;

    return (struct usual_arithmetic_conversions_result){
        .float_rank_selected = USUAL_CONVERSION_KIND(1.0f + 2.0),
        .unsigned_wrap_greater = (signed_negative + unsigned_value) > 10u,
        .long_double_rank_selected = USUAL_CONVERSION_KIND(1.0L + 2.0),
    };
}
