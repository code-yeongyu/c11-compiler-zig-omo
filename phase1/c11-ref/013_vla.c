#include <stddef.h>

struct vla_result {
    int supported;
    int sum;
};

#if defined(__STDC_NO_VLA__)
static int vla_weighted_sum(size_t count, const int *values)
{
    int total = 0;
    for (size_t index = 0u; index < count; index += 1u) {
        total += values[index];
    }
    return total;
}
#else
static int vla_weighted_sum(size_t count, const int values[count])
{
    int copy[count];
    int total = 0;
    for (size_t index = 0u; index < count; index += 1u) {
        copy[index] = values[index] * (int)(index + 1u);
        total += copy[index];
    }
    return total;
}
#endif

struct vla_result vla_run(void)
{
    const int values[] = { 3, 5, 7 };
    return (struct vla_result){
#if defined(__STDC_NO_VLA__)
        .supported = 0,
#else
        .supported = 1,
#endif
        .sum = vla_weighted_sum(sizeof values / sizeof values[0], values),
    };
}
