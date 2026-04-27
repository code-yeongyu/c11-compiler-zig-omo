#include <limits.h>
#include <stddef.h>

struct static_assert_record {
    char tag;
    int value;
};

_Static_assert(CHAR_BIT == 8, "suite assumes octet bytes");
_Static_assert(sizeof(struct static_assert_record) >= sizeof(int), "record stores an int");
_Static_assert(offsetof(struct static_assert_record, value) > 0u, "value is not first");

int static_assert_runtime_value(void)
{
    struct static_assert_record record = { .tag = 's', .value = 77 };
    return record.value;
}
