#include "test_support.h"
#include "021_fopen_x_mode.c"

int main(void)
{
    /* given */
    const int expected_success = 1;

    /* when */
    const struct fopen_x_mode_result result = fopen_x_mode_run();

    /* then */
    assert(result.first_open_succeeded == expected_success);
    assert(result.second_open_failed == expected_success);
    assert(result.errno_was_eexist == expected_success);
    C11_REF_OK();
}
