#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#if defined(__has_include)
#if __has_include(<uchar.h>)
#include <uchar.h>
#else
typedef uint_least16_t char16_t;
typedef uint_least32_t char32_t;
#endif
#else
#include <uchar.h>
#endif

struct unicode_literals_result {
    size_t utf8_bytes;
    size_t utf16_units;
    size_t utf32_units;
    size_t wide_units;
    char utf8_first;
    char16_t utf16_first;
    char32_t utf32_first;
    wchar_t wide_first;
};

struct unicode_literals_result unicode_literals_run(void)
{
    static const char utf8_text[] = u8"C11 \u03bb";
    static const char16_t utf16_text[] = u"Az";
    static const char32_t utf32_text[] = U"\U0001F642";
    static const wchar_t wide_text[] = L"Wx";

    return (struct unicode_literals_result){
        .utf8_bytes = sizeof utf8_text,
        .utf16_units = sizeof utf16_text / sizeof utf16_text[0],
        .utf32_units = sizeof utf32_text / sizeof utf32_text[0],
        .wide_units = sizeof wide_text / sizeof wide_text[0],
        .utf8_first = utf8_text[0],
        .utf16_first = utf16_text[0],
        .utf32_first = utf32_text[0],
        .wide_first = wide_text[0],
    };
}
