#include <stddef.h>
#include <stdalign.h>

struct alignas_alignof_bucket {
    char prefix;
    alignas(32) unsigned char bytes[32];
};

struct alignas_alignof_result {
    size_t bucket_alignment;
    size_t byte_alignment;
    size_t bytes_offset;
    size_t max_alignment;
};

_Static_assert(alignof(struct alignas_alignof_bucket) >= 32, "alignas raises aggregate alignment");
_Static_assert(offsetof(struct alignas_alignof_bucket, bytes) % 32 == 0, "member offset respects alignas");

struct alignas_alignof_result alignas_alignof_run(void)
{
    alignas(0) char natural = 'x';
    (void)natural;
    return (struct alignas_alignof_result){
        .bucket_alignment = alignof(struct alignas_alignof_bucket),
        .byte_alignment = alignof(unsigned char),
        .bytes_offset = offsetof(struct alignas_alignof_bucket, bytes),
        .max_alignment = alignof(max_align_t),
    };
}
