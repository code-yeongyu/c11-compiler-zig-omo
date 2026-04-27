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
    assert(result.supported == threads_available);
    if (threads_available) {
        assert(result.main_value == 5);
        assert(result.worker_value == 42);
    }
    C11_REF_OK();
}
