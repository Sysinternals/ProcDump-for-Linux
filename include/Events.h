// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// Quick events implementation
//
//--------------------------------------------------------------------

#ifndef EVENTS_H
#define EVENTS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>

#define MAX_EVENT_NAME 64

#define MANUAL_RESET_EVENT_INITIALIZER(NAME) \
    {\
    .mutex          = PTHREAD_MUTEX_INITIALIZER,\
    .cond           = PTHREAD_COND_INITIALIZER,\
    .bManualReset   = true,\
    .bTriggered     = false,\
    .nWaiters       = 0,\
    .Name           = NAME\
    }

struct Event {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool bTriggered;
    bool bManualReset;
    char Name[MAX_EVENT_NAME];
    int nWaiters;
};

struct Event *CreateEvent(bool IsManualReset, bool InitialState);
struct Event *CreateNamedEvent(bool IsManualReset, bool InitialState, char *Name);
void InitEvent(struct Event *Event, bool IsManualReset, bool InitialState);
void InitNamedEvent(struct Event *Event, bool IsManualReset, bool InitialState, char *Name);
void DestroyEvent(struct Event *Event);
bool SetEvent(struct Event *Event);
bool ResetEvent(struct Event *Event);

#endif // EVENTS_H