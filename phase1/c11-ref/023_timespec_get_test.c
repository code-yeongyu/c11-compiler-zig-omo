#include "test_support.h"
#include "023_timespec_get.c"

int main(void)
{
    /* given */
    const long maximum_delta_seconds = 2L;

    /* when */
    const struct timespec_get_result result = timespec_get_run();

    /* then */
    assert(result.returned_base == TIME_UTC);
    assert(result.delta_from_time <= maximum_delta_seconds);
    C11_REF_OK();
}
