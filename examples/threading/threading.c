#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

bool sleep_ms(int64_t ms)
{
    struct timespec t = {0, ms*1000};
    int ret = nanosleep(&t, NULL);
    return (ret == 0);
}

void* threadfunc(void* thread_param)
{
    thread_data *t = (thread_data *) thread_param;
    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    bool slept = sleep_ms(t->wait_to_obtain_ms);

    int ret = pthread_mutex_lock(t->mutex);
    if (ret != 0) return thread_param;

    t->a++;
    slept &= sleep_ms(t->wait_to_release_ms);
    pthread_mutex_unlock(t->mutex);

    t->thread_complete_success = slept;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    thread_data *t = malloc(sizeof(*t));
    if (t == NULL) return false;
    t->a = 0;
    t->mutex = mutex;
    t->wait_to_obtain_ms = wait_to_obtain_ms;
    t->wait_to_release_ms = wait_to_release_ms;
    t->thread_complete_success = false;

    int ret = pthread_create(thread, NULL, threadfunc, t);
    return (ret == 0);
}

