#include <stdlib.h>
#include <time.h>

struct timespec_get_result {
    int returned_base;
    long delta_from_time;
};

struct timespec_get_result timespec_get_run(void)
{
    struct timespec spec = { 0, 0 };
    const time_t now = time(NULL);
    const int base = timespec_get(&spec, TIME_UTC);
    return (struct timespec_get_result){
        .returned_base = base,
        .delta_from_time = labs((long)(spec.tv_sec - now)),
    };
}
