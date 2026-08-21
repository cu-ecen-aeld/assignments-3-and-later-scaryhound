#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure

    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    struct timespec ts;
    
    ts.tv_sec = thread_func_args->wait_to_obtain_ms / 1000;
    ts.tv_nsec = (thread_func_args->wait_to_obtain_ms % 1000)*1000000;
    nanosleep(&ts, NULL);
    
    pthread_mutex_lock(thread_func_args->mutex);
    

    ts.tv_sec += thread_func_args->wait_to_release_ms / 1000;
    ts.tv_nsec += (thread_func_args->wait_to_release_ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
    

    
    pthread_mutex_unlock(thread_func_args->mutex);
    
    thread_func_args->thread_complete_success = true;
    
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
     
     struct thread_data* data = (struct thread_data*) malloc(sizeof(struct thread_data));
     
     if(data == NULL)
     {
     	ERROR_LOG("Failed to allocate memory for thread_data");
     	return false;
     }
     
     data->wait_to_obtain_ms = wait_to_obtain_ms;
     data->wait_to_release_ms = wait_to_release_ms;
     data->mutex = mutex;
     data->thread_complete_success = false;
     
     int rc = pthread_create(thread, NULL, threadfunc, data);
     
     if(rc !=0)
     {
     	ERROR_LOG("Failed to create thread. Error code: %d", rc);
     	free(data);
     	return false;
     }
     
     
    return true;
}

