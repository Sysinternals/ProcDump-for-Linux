// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// Core dump orchestrator
//
//--------------------------------------------------------------------
#include "Includes.h"

static const char *CoreDumpTypeStrings[] = { "commit", "cpu", "thread", "filedesc", "signal", "time", "exception", "manual" };

//--------------------------------------------------------------------
//
// NewCoreDumpWriter - Helper function for newing a struct CoreDumpWriter
//
// Returns: struct CoreDumpWriter *
//
//--------------------------------------------------------------------
struct CoreDumpWriter *NewCoreDumpWriter(enum ECoreDumpType type, struct ProcDumpConfiguration *config)
{
    struct CoreDumpWriter *writer = (struct CoreDumpWriter *)malloc(sizeof(struct CoreDumpWriter));
    if (writer == NULL) {
        Log(error, INTERNAL_ERROR);
        Trace("NewCoreDumpWriter: failed to allocate memory.");
        exit(-1);
    }

    writer->Config = config;
    writer->Type = type;

    return writer;
}


//--------------------------------------------------------------------
//
// GetCoreDumpName - Gets the core dump name
//
//--------------------------------------------------------------------
char* GetCoreDumpName(pid_t pid, char* procName, char* dumpPath, char* dumpName, enum ECoreDumpType type)
{
    auto_free char *name = sanitize(procName);
    time_t rawTime = {0};
    struct tm* timerInfo = NULL;
    char date[DATE_LENGTH];
    char* gcorePrefixName = NULL;

    gcorePrefixName = malloc(PATH_MAX+1);
    if(!gcorePrefixName)
    {
        Log(error, INTERNAL_ERROR);
        Trace("GetCoreDumpName: Memory allocation failure");
        exit(-1);
    }

    const char *desc = CoreDumpTypeStrings[type];

    // get time for current dump generated
    rawTime = time(NULL);
    if((timerInfo = localtime(&rawTime)) == NULL){
        Log(error, INTERNAL_ERROR);
        Trace("GetCoreDumpName: failed localtime");
        exit(-1);
    }
    strftime(date, 26, "%Y-%m-%d_%H:%M:%S", timerInfo);

    // assemble the full file name (including path) for core dumps
    if(dumpName != NULL)
    {
        if(snprintf(gcorePrefixName, BUFFER_LENGTH, "%s/%s", dumpPath, dumpName) < 0)
        {
            Log(error, INTERNAL_ERROR);
            Trace("GetCoreDumpName: failed sprintf custom output file name");
            exit(-1);
        }
    }
    else
    {
        if(snprintf(gcorePrefixName, BUFFER_LENGTH, "%s/%s_%s_%s", dumpPath, name, desc, date) < 0)
        {
            Log(error, INTERNAL_ERROR);
            Trace("GetCoreDumpName: failed sprintf default output file name");
            exit(-1);
        }
    }

    return gcorePrefixName;
}

//--------------------------------------------------------------------
//
// WaitForQuit - Wait for Quit Event or just timeout
//
//      Timed wait with awareness of quit event
//
// Returns: 0   - Success
//          -1  - Failure
//          1   - Quit/Limit reached
//
//--------------------------------------------------------------------
int WriteCoreDump(struct CoreDumpWriter *self)
{
    int rc = 0;

    // Enter critical section (block till we decrement semaphore)
    rc = WaitForQuitOrEvent(self->Config, &self->Config->semAvailableDumpSlots, INFINITE_WAIT);
    if(rc == 0){
        Log(error, INTERNAL_ERROR);
        Trace("WriteCoreDump: failed WaitForQuitOrEvent.");
        exit(-1);
    }
    if(pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL) != 0){
        Log(error, INTERNAL_ERROR);
        Trace("WriteCoreDump: failed pthread_setcanceltype.");
        exit(-1);
    }
    switch (rc) {
        case WAIT_OBJECT_0: // QUIT!  Time for cleanup, no dump
            break;
        case WAIT_OBJECT_0+1: // We got a dump slot!
            {
                char* socketName = NULL;
                IsCoreClrProcess(self->Config->ProcessId, &socketName);
                if ((rc = WriteCoreDumpInternal(self, socketName)) == 0) {
                    // We're done here, unlock (increment) the sem
                    if(sem_post(&self->Config->semAvailableDumpSlots.semaphore) == -1){
                        Log(error, INTERNAL_ERROR);
                        Trace("WriteCoreDump: failed sem_post.");
                        if(socketName) free(socketName);
                        exit(-1);
                    }
                }
                if(socketName) free(socketName);
            }
            break;
        case WAIT_ABANDONED: // We've hit the dump limit, clean up
            break;
        default:
            Trace("WriteCoreDump: Error in default case");
            break;
    }
    if(pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL) != 0){
        Log(error, INTERNAL_ERROR);
        Trace("WriteCoreDump: failed pthread_setcanceltype.");
        exit(-1);
    }

    return rc;
}

// --------------------------------------------------------------------------------------
// CRITICAL SECTION
// Should only ever have <max number of dump slots> running concurrently
// The default value of which is 1 (hard coded) and is set in
// ProcDumpConfiguration.semAvailableDumpSlots
// Returns 1 if we trigger quit in the crit section, 0 otherwise
// --------------------------------------------------------------------------------------
int WriteCoreDumpInternal(struct CoreDumpWriter *self, char* socketName)
{
    char command[BUFFER_LENGTH];
    char ** outputBuffer;
    char lineBuffer[BUFFER_LENGTH];
    char coreDumpFileName[PATH_MAX+1] = {0};
    auto_free char* gcorePrefixName = NULL;
    int  lineLength;
    int  i = 0;
    int  rc = 0;
    pid_t gcorePid;
    FILE *commandPipe = NULL;

    char *name = sanitize(self->Config->ProcessName);
    pid_t pid = self->Config->ProcessId;

    gcorePrefixName = GetCoreDumpName(self->Config->ProcessId, name, self->Config->CoreDumpPath, self->Config->CoreDumpName, self->Type);

    // assemble the command
    if(snprintf(command, BUFFER_LENGTH, "gcore -o %s %d 2>&1", gcorePrefixName, pid) < 0)
    {
        Log(error, INTERNAL_ERROR);
        Trace("WriteCoreDumpInternal: failed sprintf gcore command");
        exit(-1);
    }

    // assemble filename
    if(snprintf(coreDumpFileName, PATH_MAX, "%s.%d", gcorePrefixName, pid) < 0)
    {
        Log(error, INTERNAL_ERROR);
        Trace("WriteCoreDumpInternal: failed sprintf core file name");
        exit(-1);
    }

    // If the file already exists and the overwrite flag has not been set we fail
    if(access(coreDumpFileName, F_OK)==0 && !self->Config->bOverwriteExisting)
    {
        Log(info, "Dump file %s already exists and was not overwritten (use -o to overwrite)", coreDumpFileName);
        return -1;
    }

    // check if we're allowed to write into the target directory
    if(access(self->Config->CoreDumpPath, W_OK) < 0)
    {
        Log(error, INTERNAL_ERROR);
        Trace("WriteCoreDumpInternal: no write permission to core dump target file %s",
              coreDumpFileName);
        exit(-1);
    }

    if(socketName!=NULL)
    {
        // If we have a socket name, we're dumping a .NET process....
        if(GenerateCoreClrDump(socketName, coreDumpFileName)==false)
        {
            Log(error, "An error occurred while generating the core dump for the specified .NET process");
        }
        else
        {
            // log out sucessful core dump generated
            Log(info, "Core dump %d generated: %s", self->Config->NumberOfDumpsCollected, coreDumpFileName);

            self->Config->NumberOfDumpsCollected++; // safe to increment in crit section
        }
    }
    else
    {
        // allocate output buffer
        outputBuffer = (char**)malloc(sizeof(char*) * MAX_LINES);
        if(outputBuffer == NULL)
        {
            Log(error, INTERNAL_ERROR);
            Trace("WriteCoreDumpInternal: failed gcore output buffer allocation");
            exit(-1);
        }

        // Oterwise, we use gcore dump generation   TODO@FUTURE: We might consider adding a forcegcore flag in cases where
        // someone wants to use gcore even for .NET processes.
        commandPipe = popen2(command, "r", &gcorePid);
        self->Config->gcorePid = gcorePid;

        if(commandPipe == NULL)
        {
            Log(error, "An error occurred while generating the core dump");
            Trace("WriteCoreDumpInternal: Failed to open pipe to gcore");
            exit(1);
        }

        // read all output from gcore command
        for(i = 0; i < MAX_LINES && fgets(lineBuffer, sizeof(lineBuffer), commandPipe) != NULL; i++)
        {
            lineLength = strlen(lineBuffer) + 1;                                // get # of characters read

            outputBuffer[i] = (char*)malloc(sizeof(char) * lineLength);
            if(outputBuffer[i] != NULL)
            {
                strcpy(outputBuffer[i], lineBuffer);
                outputBuffer[i][lineLength-1] = '\0';                           // append null character
            }
            else
            {
                Log(error, INTERNAL_ERROR);
                Trace("WriteCoreDumpInternal: failed to allocate gcore error message buffer");
                exit(-1);
            }
        }

        // After reading all input, wait for child process to end and get exit status for bash gcore command
        int stat;
        waitpid(gcorePid, &stat, 0);
        int gcoreStatus = WEXITSTATUS(stat);

        // close pipe reading from gcore
        self->Config->gcorePid = NO_PID;                // reset gcore pid so that signal handler knows we aren't dumping
        int pcloseStatus = pclose(commandPipe);

        bool gcoreFailedMsg = false;    // in case error sneaks through the message output

        // check if gcore was able to generate the dump
        if(gcoreStatus != 0 || pcloseStatus != 0 || (gcoreFailedMsg = (strstr(outputBuffer[i-1], "gcore: failed") != NULL)))
        {
            Log(error, "An error occurred while generating the core dump:");
            if (gcoreStatus != 0)
                Log(error, "\tDump exit status = %d", gcoreStatus);
            if (pcloseStatus != 0)
                Log(error, "\tError closing pipe: %d", pcloseStatus);
            if (gcoreFailedMsg)
                Log(error, "\tgcore failed");

            // log gcore message
            for(int j = 0; j < i; j++)
            {
                if(outputBuffer[j] != NULL)
                {
                    Log(error, "GCORE - %s", outputBuffer[j]);
                }
            }

            rc = gcoreStatus;
        }
        else
        {
            // On WSL2 there is a delay between the core dump being written to disk and able to succesfully access it in the below check
            sleep(1);

            // validate that core dump file was generated
            if(access(coreDumpFileName, F_OK) != -1)
            {
                if(self->Config->nQuit)
                {
                    // if we are in a quit state from interrupt delete partially generated core dump file
                    int ret = unlink(coreDumpFileName);
                    if (ret < 0 && errno != ENOENT)
                    {
                        Trace("WriteCoreDumpInternal: Failed to remove partial core dump");
                        exit(-1);
                    }
                }
                else
                {
                    // log out sucessful core dump generated
                    Log(info, "Core dump %d generated: %s", self->Config->NumberOfDumpsCollected, coreDumpFileName);

                    self->Config->NumberOfDumpsCollected++; // safe to increment in crit section
                    if (self->Config->NumberOfDumpsCollected >= self->Config->NumberOfDumpsToCollect)
                    {
                        SetEvent(&self->Config->evtQuit.event); // shut it down, we're done here
                        rc = 1;
                    }
                }
            }
        }

        for(int j = 0; j < i; j++)
        {
            free(outputBuffer[j]);
        }
        free(outputBuffer);
    }


    free(name);

    return rc;
}

