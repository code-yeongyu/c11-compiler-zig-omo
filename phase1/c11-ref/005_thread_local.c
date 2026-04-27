#if !defined(__STDC_NO_THREADS__)
#include <threads.h>
#endif

struct thread_local_result {
    int threads_supported;
    int main_value;
    int worker_value;
};

static _Thread_local int thread_local_counter;

#if !defined(__STDC_NO_THREADS__)
static int thread_local_worker(void *argument)
{
    int *output = argument;
    thread_local_counter = 41;
    thread_local_counter += 1;
    *output = thread_local_counter;
    return 0;
}
#endif

struct thread_local_result thread_local_run(void)
{
    int worker_value = 0;
    thread_local_counter = 5;
#if !defined(__STDC_NO_THREADS__)
    thrd_t thread;
    if (thrd_create(&thread, thread_local_worker, &worker_value) == thrd_success) {
        (void)thrd_join(thread, 0);
    }
#endif
    return (struct thread_local_result){
        .threads_supported =
#if defined(__STDC_NO_THREADS__)
            0,
#else
            1,
#endif
        .main_value = thread_local_counter,
        .worker_value = worker_value,
    };
}
