#include "test_support.h"
#include "001_generic_selection.c"

int main(void)
{
    /* given */
    const int character_literal_is_int = (int)'a' + 1;

    /* when */
    const struct generic_selection_result result = generic_selection_run();

    /* then */
    assert(result.char_result == 7);
    assert(result.int_result == 8);
    assert(result.literal_result == character_literal_is_int);
    assert(result.qualified_result == 9);
    assert(result.pointer_result == 10);
    C11_REF_OK();
}
