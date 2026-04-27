#include "test_support.h"
#include "009_unicode_literals.c"

int main(void)
{
    /* given */
    const size_t expected_utf8_bytes = 7u;
    const size_t expected_utf16_units = 3u;
    const size_t expected_utf32_units = 2u;

    /* when */
    const struct unicode_literals_result result = unicode_literals_run();

    /* then */
    assert(result.utf8_bytes == expected_utf8_bytes);
    assert(result.utf16_units == expected_utf16_units);
    assert(result.utf32_units == expected_utf32_units);
    assert(result.wide_units == 3u);
    assert(result.utf8_first == 'C');
    assert(result.utf16_first == (char16_t)'A');
    assert(result.utf32_first == (char32_t)0x0001F642u);
    assert(result.wide_first == L'W');
    C11_REF_OK();
}
