#include <stdalign.h>
#include <stddef.h>

typedef unsigned corner_size;
typedef unsigned corner_size;

struct corner_cases_aligned {
    alignas(0) char natural;
    alignas(16) int payload;
};

#define CORNER_KIND(value) \
    _Generic((value), \
        int: 1, \
        char: 2, \
        default: 3 \
    )

struct corner_cases_result {
    int char_constant_kind;
    int func_name_nonempty;
    size_t aligned_offset;
    corner_size typedef_value;
};

struct corner_cases_result corner_cases_run(void)
{
    const char *name = __func__;
    return (struct corner_cases_result){
        .char_constant_kind = CORNER_KIND('q'),
        .func_name_nonempty = name[0] != '\0',
        .aligned_offset = offsetof(struct corner_cases_aligned, payload),
        .typedef_value = (corner_size)33u,
    };
}
