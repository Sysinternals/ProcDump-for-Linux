// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// Generalization of Events and Semaphores (Critical Sections)
//
//--------------------------------------------------------------------

#ifndef HANDLE_H
#define HANDLE_H

#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "Events.h"
#include "Logging.h"

#define INFINITE_WAIT -1
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT ETIMEDOUT
#define WAIT_ABANDONED 0x80

#define HANDLE_MANUAL_RESET_EVENT_INITIALIZER(NAME) \
{\
    {\
        .event = {\
            .mutex          = PTHREAD_MUTEX_INITIALIZER,\
            .cond           = PTHREAD_COND_INITIALIZER,\
            .bManualReset   = true,\
            .bTriggered     = false,\
            .nWaiters       = 0,\
            .Name           = NAME\
        }\
    },\
    .type = EVENT\
}

enum EHandleType {
    EVENT,
    SEMAPHORE
};

struct Handle {
    union {
        struct Event event;
        sem_t semaphore;
    };
    enum EHandleType type;
};

int WaitForSingleObject(struct Handle *Handle, int Milliseconds);
int WaitForMultipleObjects(int Count, struct Handle **Handles, bool WaitAll, int Milliseconds);

#endif // HANDLE_H