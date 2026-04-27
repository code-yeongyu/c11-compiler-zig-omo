#include "test_support.h"
#include "002_alignas_alignof.c"

int main(void)
{
    /* given */
    const size_t requested_alignment = 32u;

    /* when */
    const struct alignas_alignof_result result = alignas_alignof_run();

    /* then */
    assert(result.bucket_alignment >= requested_alignment);
    assert(result.bytes_offset % requested_alignment == 0u);
    assert(result.byte_alignment == 1u);
    assert(result.max_alignment >= result.byte_alignment);
    C11_REF_OK();
}
