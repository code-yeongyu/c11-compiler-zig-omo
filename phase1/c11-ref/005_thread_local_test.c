#include "test_support.h"
#include "005_thread_local.c"

int main(void)
{
    /* given */
    const int threads_available =
#if defined(__STDC_NO_THREADS__)
        0;
#else
        1;
#endif

    /* when */
    const struct thread_local_result result = thread_local_run();

    /* then */
    assert(result.threads_supported == threads_available);
    assert(result.main_value == 5);
    if (threads_available) {
        assert(result.worker_value == 42);
    } else {
        assert(result.worker_value == 0);
    }
    C11_REF_OK();
}
