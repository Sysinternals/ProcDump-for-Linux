// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// Quick events implementation
//
//--------------------------------------------------------------------

#include "Events.h"
#include "Logging.h"


//--------------------------------------------------------------------
//
// CreateEvent - Create an Event and return a pointer to it
//
//--------------------------------------------------------------------
struct Event *CreateEvent(bool IsManualReset, bool InitialState)
{
    struct Event *event = (struct Event *)malloc(sizeof(struct Event));
    if(event == NULL){
        Log(error, "INTERNAL_ERROR");
        Trace("CreateEvent: failed memory allocation.");        
        exit(-1);
    }
    InitEvent(event, IsManualReset, InitialState);

    return event;
}


//--------------------------------------------------------------------
//
// CreateNamedEvent - Create a Named Event and return a pointer to it
//
//--------------------------------------------------------------------
struct Event *CreateNamedEvent(bool IsManualReset, bool InitialState, char *Name)
{
    struct Event *event = (struct Event *)malloc(sizeof(struct Event));
    if(event == NULL){
        Log(error, INTERNAL_ERROR);
        Trace("CreateNamedEvent: failed memory allocation.");        
        exit(-1);
    }

    InitNamedEvent(event, IsManualReset, InitialState, Name);

    return event;
}


//--------------------------------------------------------------------
//
// InitEvent - initialize an Event
//
//--------------------------------------------------------------------
void InitEvent(struct Event *Event, bool IsManualReset, bool InitialState)
{
    InitNamedEvent(Event, IsManualReset, InitialState, NULL);
}


//--------------------------------------------------------------------
//
// InitNamedEvent - Initialize a Named Event
//
//--------------------------------------------------------------------
void InitNamedEvent(struct Event *Event, bool IsManualReset, bool InitialState, char *Name)
{
    static int unamedEventId = 0; // ID for logging purposes

    pthread_mutex_init(&(Event->mutex), NULL);
    if(pthread_cond_init(&(Event->cond), NULL) != 0){
        Log(error, INTERNAL_ERROR);
        Trace("InitNamedEvent: failed pthread_cond_init.");
        exit(-1);
    }
    Event->bManualReset = IsManualReset;
    Event->bTriggered = InitialState;
    Event->nWaiters = 0;

    if (Name == NULL) {
        sprintf(Event->Name, "Unamed Event %d", ++unamedEventId);  
    } else if (strlen(Name) >= MAX_EVENT_NAME) {
        strncpy(Event->Name, Name, MAX_EVENT_NAME);
        Event->Name[MAX_EVENT_NAME - 1] = '\0'; // null terminate
    } else {
        strcpy(Event->Name, Name);
    }
}


//--------------------------------------------------------------------
//
// DestroyEvent - Clean up an event
//
//--------------------------------------------------------------------
void DestroyEvent(struct Event *Event)
{
    // destroy mutex and cond
    if(pthread_cond_destroy(&(Event->cond)) != 0){
        Log(error, INTERNAL_ERROR);
        Trace("DestroyEvent: failed pthread_cond_destroy.");        
        exit(-1);
    }
    if(pthread_mutex_destroy(&(Event->mutex)) != 0){
        Log(error, INTERNAL_ERROR);
        Trace("DestroyEvent: failed pthread_mutex_destroy.");        
        exit(-1);
    }
}

//--------------------------------------------------------------------
//
// SetEvent - Attempts to trigger the event and set Event.bTriggered to true
//
// Return - A boolean indicating success of firing the event
//
//--------------------------------------------------------------------
bool SetEvent(struct Event *Event)
{
    int success = 0;

    if ((success = pthread_mutex_lock(&(Event->mutex))) == 0) {     
        Event->bTriggered = true;
        Event->bManualReset ? // Are we a manual-reset?
            pthread_cond_broadcast(&(Event->cond)) :    // signal everyone!
            pthread_cond_signal(&(Event->cond));        // Signal first in line!
        pthread_mutex_unlock(&(Event->mutex));
    }
    else
    {
        Log(error, INTERNAL_ERROR);
        Trace("SetEvent: failed pthread_mutex_lock.");        
        exit(-1);
    }

    return (success == 0);
}

//--------------------------------------------------------------------
//
// ResetEvent - For Events with bManualReset == true
//
// Return - A boolean indicating success of reseting the event (i.e., Event.bTriggered == false)
//
//--------------------------------------------------------------------
bool ResetEvent(struct Event *Event)
{
    int success = 0;

    if ((success = pthread_mutex_lock(&(Event->mutex))) == 0) {
        Event->bTriggered = false;
        if(pthread_mutex_unlock(&(Event->mutex)) != 0){
            Log(error, INTERNAL_ERROR);
            Trace("ResetEvent: failed pthread_mutex_unlock.");        
            exit(-1);        
        }
    }
    else{
        Log(error, INTERNAL_ERROR);
        Trace("ResetEvent: failed pthread_mutex_lock.");        
        exit(-1);        
    }

    return (success == 0);
}


