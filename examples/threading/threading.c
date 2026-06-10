// Bhumika Sood

#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    // Obtain thread arguments from parameter
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    // Return error if params are null
    if (thread_param == NULL)
    {
        ERROR_LOG("Input params are NULL");
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    // Wait to obtain mutex
    usleep(thread_func_args->wait_to_obtain_ms * 1000);

    // Obtain mutex
    int result = pthread_mutex_lock(thread_func_args->mutex);
    
    // Return error if failed to obtain mutex
    if (result != 0)
    {
        ERROR_LOG("Failed to obtain mutex");
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    // Wait to release mutex
    usleep(thread_func_args->wait_to_release_ms * 1000);

    // Release mutex
    result = pthread_mutex_unlock(thread_func_args->mutex);

    // Return error if failed to release mutex
    if (result != 0)
    {
        ERROR_LOG("Failed to release mutex");
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    // Thread completed successfully
    if (result == 0)
    {
        thread_func_args->thread_complete_success = true;
    }

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms)
{
    // Create thread data struct
    struct thread_data *data = malloc(sizeof(struct thread_data));

    // Check if memory was allocated
    if (data == NULL)
    {
        ERROR_LOG("Failed to create thread data struct");
        return false;
    }

    // Store data in struct
    data->mutex = mutex;
    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;

    // Create thread
    int result = pthread_create(thread, NULL, threadfunc, data);

    // Check if thread is created successfully
    if (result != 0)
    {
        ERROR_LOG("Failed to create thread");
        free(data);
        return false;
    }

    return true;
}

