#include "test_support.h"
#include "020_gets_removal.c"

int main(void)
{
    /* given */
    const int expected_line_bytes = 4;

    /* when */
    const struct gets_removal_result result = gets_removal_run();

    /* then */
    assert(result.gets_macro_absent == 1);
    assert(result.getline_read_bytes == expected_line_bytes);
    assert(result.annex_k_gets_s_available == 0 || result.annex_k_gets_s_available == 1);
    C11_REF_OK();
}
