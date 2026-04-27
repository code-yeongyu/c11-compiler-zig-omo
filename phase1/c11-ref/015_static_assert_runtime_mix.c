#include <stddef.h>

struct static_assert_runtime_mix_header {
    unsigned short magic;
    unsigned char version;
    unsigned char flags;
};

_Static_assert(sizeof(struct static_assert_runtime_mix_header) == 4u, "header stays packed by member sizes");
_Static_assert(offsetof(struct static_assert_runtime_mix_header, flags) == 3u, "flags byte is fourth");

int static_assert_runtime_mix_run(void)
{
    const struct static_assert_runtime_mix_header header = {
        .magic = 0xC011u,
        .version = 1u,
        .flags = 2u,
    };
    return (int)header.magic + (int)header.version + (int)header.flags;
}
