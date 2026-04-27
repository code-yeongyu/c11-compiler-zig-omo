#include <stdint.h>
#include <stdlib.h>

struct aligned_alloc_result {
    int allocated;
    int aligned;
};

struct aligned_alloc_result aligned_alloc_run(void)
{
    const size_t alignment = 32u;
    const size_t size = 64u;
    void *memory = aligned_alloc(alignment, size);
    const int aligned = memory != 0 && ((uintptr_t)memory % alignment) == 0u;
    free(memory);
    return (struct aligned_alloc_result){
        .allocated = memory != 0,
        .aligned = aligned,
    };
}
