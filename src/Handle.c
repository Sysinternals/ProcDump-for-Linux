// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// Generalization of Events and Semaphores (Critical Sections)
//
//--------------------------------------------------------------------

#include "Handle.h"

//--------------------------------------------------------------------
//
// WaitForSingleObject - Blocks the current thread until
//      either the event triggers, semaphore > 0,
//       or the wait time has passed
//
// Parameters:
//      -Handle -> the event/semaphore to wait for
//      -Milliseconds -> the time to wait (in milliseconds).
//          '-1' will mean infinite, and 0 will be instant check
//
// Return - An integer indicating state of wait
//      0 -> successful wait, and trigger fired
//      ETIMEDOUT -> the wait timed out (based on sepcified milliseconds)
//      other non-zero -> check errno.h
//
//--------------------------------------------------------------------
int WaitForSingleObject(struct Handle *Handle, int Milliseconds)
{
    struct timespec ts;
    int rc = 0;

    // Get current time and add wait time
    if (Milliseconds != INFINITE_WAIT)
    { // We aren't waiting infinitely
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += Milliseconds / 1000;              // ms->sec
        ts.tv_nsec += (Milliseconds % 1000) * 1000000; // remaining ms->ns
    }

    switch (Handle->type) {
    case EVENT:
        if ((rc = pthread_mutex_lock(&(Handle->event.mutex))) == 0)
        {
            Handle->event.nWaiters++;
            while (!Handle->event.bTriggered && rc == 0)
            {
                rc = (Milliseconds == INFINITE_WAIT) ? // either wait
                    pthread_cond_wait(&(Handle->event.cond), &(Handle->event.mutex)) :  // infinitely
                    pthread_cond_timedwait(&(Handle->event.cond), &(Handle->event.mutex), &ts); // or till specified time passes
            }
            Handle->event.nWaiters--;

            // Check if we should reset
            if (Handle->event.nWaiters == 0 && !Handle->event.bManualReset)
            {
                Handle->event.bTriggered = false;
            }
            pthread_mutex_unlock(&(Handle->event.mutex));
        }

        
        break;

    case SEMAPHORE:
        rc = (Milliseconds == INFINITE_WAIT) ? 
            sem_wait(&(Handle->semaphore)) :
            sem_timedwait(&(Handle->semaphore), &ts);
        break;
    
    default:
        rc = -1;
        break;
    }

    return rc;
}

// Helper functions/infrastructure for WaitForMultipleObjects
struct thread_result {
    int retVal;
    int threadIndex;
};

struct coordinator {
    pthread_cond_t condEventTriggered;
    pthread_mutex_t mutexEventTriggered;
    struct thread_result *results;
    int numberTriggered; // behind mutex
    int nWaiters; // when 0, delete the struct
    int stopIssued; // when != 0, proceed to cleanup
    struct Handle evtCanCleanUp; // trigger when we leave main wait thread
    struct Handle evtStartWaiting;
};

struct thread_args {
    struct Handle *handle;
    struct coordinator *coordinator; // for cleanup
    int milliseconds;
    int retVal;
    int threadIndex;
};

void *WaiterThread(void *thread_args)
{
    int rc;
    struct thread_args *input = (struct thread_args *)thread_args;

    // Wait for go signal
    if ((rc = WaitForSingleObject(&(input->coordinator->evtStartWaiting), 2000)) != WAIT_OBJECT_0) {
        // we messed up and the thread can't start...
    }

    // wait on the event, and then let parent know if we signal
    if (input->milliseconds == INFINITE_WAIT) {
        do {
            rc = WaitForSingleObject(input->handle, 5000); // loop every 5 seconds to make sure we can get out and cleanup if we don't need to wait anymore
        } while (!input->coordinator->stopIssued && rc == ETIMEDOUT);
    } else {
        rc = WaitForSingleObject(input->handle, input->milliseconds); // blocks till timeout, error, or success
    }


    pthread_mutex_lock(&input->coordinator->mutexEventTriggered);
    struct thread_result result = { .retVal = rc, .threadIndex = input->threadIndex };
    input->coordinator->results[input->coordinator->numberTriggered++] = result;
    pthread_mutex_unlock(&input->coordinator->mutexEventTriggered);
    pthread_cond_signal(&input->coordinator->condEventTriggered);

    // Wait for the cleanup signal!
    WaitForSingleObject(&input->coordinator->evtCanCleanUp, INFINITE_WAIT);

    // Lock mutex to make sure each thread gets a chance to check status
    pthread_mutex_lock(&input->coordinator->mutexEventTriggered);
    input->coordinator->nWaiters--;

    if (input->coordinator->nWaiters == 0) { // if we're the last one, turn the lights out
        pthread_mutex_unlock(&input->coordinator->mutexEventTriggered);
        pthread_mutex_destroy(&input->coordinator->mutexEventTriggered);
        pthread_cond_destroy(&input->coordinator->condEventTriggered);
        free(input->coordinator->results);
        free(input->coordinator);
        free(input);

    } else { // otherwise only clean up your own memory
        pthread_mutex_unlock(&input->coordinator->mutexEventTriggered);
        free(input);

    }

    pthread_exit(NULL);
}

//--------------------------------------------------------------------
//
// WaitForMultipleObjects - Blocks the current thread and waits for multiple Events
//
// Parameters:
//      -Count -> The number of Events
//      -Events -> An array of pointers to Events
//      -WaitAll -> Should we wait for all the events or whatever comes back first
//      -Milliseconds -> the number of milliseconds to wait, -1 is infinite
//
// Return - An integer indicating state of wait:
//      WAIT_OBJECT_0 to (WAIT_OBJECT_0 + Count-1) -> successful wait, and trigger fired
//              if WaitAll is TRUE: indicates all objects signaled
//              if WaitAll is FALSE: returns the index of the event that satisfied the wait *first*
//      ETIMEDOUT -> the wait timed out (based on sepcified milliseconds)
//      other non-zero -> check errno.h
//
//--------------------------------------------------------------------
int WaitForMultipleObjects(int Count, struct Handle **Handles, bool WaitAll, int Milliseconds)
{
    struct coordinator *coordinator;
    struct thread_result *results;
    
    pthread_t *threads;
    struct thread_args **thread_args;

    struct timespec ts;

    int t;
    int rc;
    int retVal;

    threads = (pthread_t *)malloc(sizeof(pthread_t) * Count);
    thread_args = (struct thread_args **)malloc(sizeof(struct thread_args *) * Count);


    coordinator = (struct coordinator *)malloc(sizeof(struct coordinator));
    if (coordinator == NULL) {
        printf("ERROR: Failed to malloc in %s\n",__FILE__);
        exit(-1);
    }

    coordinator->numberTriggered = 0;
    coordinator->stopIssued = 0;

    coordinator->evtCanCleanUp.type = EVENT;
    coordinator->evtStartWaiting.type = EVENT;
    InitNamedEvent(&(coordinator->evtCanCleanUp.event), true, false, "CanCleanUp");
    InitNamedEvent(&(coordinator->evtStartWaiting.event), true, false, "StartWaiting");
    pthread_cond_init(&coordinator->condEventTriggered, NULL);
    pthread_mutex_init(&coordinator->mutexEventTriggered, NULL);

    results = coordinator->results = (struct thread_result *)malloc(sizeof(struct thread_result) * Count);

    // Get current time and add wait time
    if (Milliseconds != -1) { // We aren't waiting infinitely
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += Milliseconds / 1000;              // ms->sec
        ts.tv_nsec += (Milliseconds % 1000) * 1000000;  // remaining ms->ns
    }

    // Create our threads
    pthread_mutex_lock(&coordinator->mutexEventTriggered);
    for (t = 0; t < Count; t++) {
        thread_args[t] = (struct thread_args *)malloc(sizeof(struct thread_args));
        if (thread_args[t] == NULL) {
            printf("ERROR: Failed to alloc in %s\n",__FILE__);
            exit(-1);
        }
        thread_args[t]->handle = Handles[t];
        thread_args[t]->milliseconds = (Milliseconds == INFINITE_WAIT) ? Milliseconds : Milliseconds + 100; // prevent race condition of everyone timing out at the same time as the main waiter thread
        thread_args[t]->threadIndex = t;
        thread_args[t]->coordinator = coordinator;
        rc = pthread_create(&threads[t], NULL, WaiterThread, (void *)thread_args[t]);
        if (rc) {
            // uh oh :(
            printf("ERROR: pthread_create failed in %s with error %d\n",__FILE__,rc);
            exit(-1);       
        }
    }

    coordinator->nWaiters = Count;
    SetEvent(&(coordinator->evtStartWaiting.event));
    
    // listen to our threads in no particular order
    while (((WaitAll && coordinator->numberTriggered < Count) ||
           (!WaitAll && coordinator->numberTriggered == 0)) &&
           rc == 0) {
        if (Milliseconds == INFINITE_WAIT) {
            if ((rc = pthread_cond_wait(&coordinator->condEventTriggered, &coordinator->mutexEventTriggered)) != 0) {
                break; // we either errored or timed out, go cleanup
            }
        } else {
            if ((rc = pthread_cond_timedwait(&coordinator->condEventTriggered, &coordinator->mutexEventTriggered, &ts)) != 0) {
                break; // we either errored or timed out, go cleanup
            }
        }
        // A handle fired.  Check if we need to kep listening or head to return
    }
    
    
    coordinator->stopIssued = 1;
    pthread_mutex_unlock(&coordinator->mutexEventTriggered);
    
    // cleanup threads
    for (t = 0; t < Count; t++) {
        pthread_detach(threads[t]);
    }

    // free everything!
    SetEvent(&(coordinator->evtCanCleanUp.event));
    
    free(threads); // we don't need handles on those threads anymore
    free(thread_args); // each thread has already got their copy and is in charge of freeing it
    
    // rc will be non-zero if we timed/errored out
    // retVal will be <wait code> + threadIndex that fired first (e.g., WAIT_OBJECT_0 + 1, WAIT_ABANDONED + 2)
    if (rc) {
        retVal = rc;
    } else {
        retVal = (WaitAll) ? rc : results[0].retVal + results[0].threadIndex;
    }

    return retVal;
}
