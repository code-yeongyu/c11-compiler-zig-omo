#if !defined(__STDC_NO_THREADS__)
#include <threads.h>
#endif

struct thread_local_result {
    int supported;
    int main_value;
    int worker_value;
};

#if !defined(__STDC_NO_THREADS__)
static _Thread_local int thread_local_counter;

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
#if defined(__STDC_NO_THREADS__)
    return (struct thread_local_result){ .supported = 0, .main_value = 0, .worker_value = 0 };
#else
    thrd_t thread;
    int worker_value = 0;
    thread_local_counter = 5;
    if (thrd_create(&thread, thread_local_worker, &worker_value) == thrd_success) {
        (void)thrd_join(thread, 0);
    }
    return (struct thread_local_result){
        .supported = 1,
        .main_value = thread_local_counter,
        .worker_value = worker_value,
    };
#endif
}
