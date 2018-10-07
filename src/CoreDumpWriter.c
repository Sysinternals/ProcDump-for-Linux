// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// Core dump orchestrator
//
//--------------------------------------------------------------------


#include "CoreDumpWriter.h"

char *sanitize(char *processName);

static const char *CoreDumpTypeStrings[] = { "commit", "cpu", "time", "manual" };

int WriteCoreDumpInternal(struct CoreDumpWriter *self);
FILE *popen2(const char *command, const char *type, pid_t *pid);

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
            if ((rc = WriteCoreDumpInternal(self)) == 0) {
                // We're done here, unlock (increment) the sem
                if(sem_post(&self->Config->semAvailableDumpSlots.semaphore) == -1){
                    Log(error, INTERNAL_ERROR);
                    Trace("WriteCoreDump: failed sem_post.");
                    exit(-1);
                }
            }
            break;
        case WAIT_ABANDONED: // We've hit the dump limit, clean up
            break;
        default:
            Trace("WriteCoreDump: Error in default case");
            break;
    }
    if(pthread_setcancelstate(PTHREAD_CANCEL_ASYNCHRONOUS, NULL) != 0){
        Log(error, INTERNAL_ERROR);
        Trace("WriteCoreDump: failed pthread_setcancelstate.");
        exit(-1);
    }

    return rc;
}

/// CRITICAL SECTION
/// Should only ever have <max number of dump slots> running concurrently
/// The default value of which is 1 (hard coded) and is set in
/// ProcDumpConfiguration.semAvailableDumpSlots
/// Returns 1 if we trigger quit in the crit section, 0 otherwise
int WriteCoreDumpInternal(struct CoreDumpWriter *self)
{
    char date[DATE_LENGTH];
    char *command = NULL;
    const char *commandFormat = "%s -o %s_%s_%s %d 2>&1";
    int commandSize;
    char ** outputBuffer;
    char lineBuffer[BUFFER_LENGTH];
    char coreDumpFileName[BUFFER_LENGTH];
    int  lineLength;
    int  i;
    int  rc = 0;
    time_t rawTime;

    pid_t gcorePid;
    struct tm* timerInfo = NULL;
    FILE *commandPipe = NULL;

    const char *desc = CoreDumpTypeStrings[self->Type];
    char *name = sanitize(self->Config->ProcessName);
    pid_t pid = self->Config->ProcessId;

    // allocate output buffer
    outputBuffer = (char**)malloc(sizeof(char*) * MAX_LINES);
    if(outputBuffer == NULL){
        Log(error, INTERNAL_ERROR);
        Trace("WriteCoreDumpInternal: failed gcore output buffer allocation");
        exit(-1);
    }

    // get time for current dump generated
    rawTime = time(NULL);
    if((timerInfo = localtime(&rawTime)) == NULL){
        Log(error, INTERNAL_ERROR);
        Trace("WriteCoreDumpInternal: failed localtime.");
        exit(-1);
    }
    strftime(date, 26, "%Y-%m-%d_%H:%M:%S", timerInfo);

    // calc command buffer size
    commandSize = snprintf(NULL, 0, commandFormat, self->Config->gcoreCmd, name, desc, date, pid);
    if (commandSize < 0) {
        Log(error, INTERNAL_ERROR);
        Trace("WriteCoreDumpInternal: failed gcore command buffer size calc");
        exit(-1);
    }

    // allocate command buffer
    command = malloc(commandSize+1);
    if (command == NULL) {
	Log(error, INTERNAL_ERROR);
	Trace("WriteCoreDumpInternal: failed gcore command buffer allocation");
	exit(-1);
    }

    // assemble the command
    if(sprintf(command, commandFormat, self->Config->gcoreCmd, name, desc, date, pid) < 0){
        Log(error, INTERNAL_ERROR);
        Trace("WriteCoreDumpInternal: failed sprintf gcore command");        
        exit(-1);
    }

    // assemble filename
    if(sprintf(coreDumpFileName, "%s_%s_%s.%d", name, desc, date, pid) < 0){
        Log(error, INTERNAL_ERROR);
        Trace("WriteCoreDumpInternal: failed sprintf core file name");        
        exit(-1);
    }

    free(name);

    // generate core dump for given process
    commandPipe = popen2(command, "r", &gcorePid);
    self->Config->gcorePid = gcorePid;
    
    if(commandPipe == NULL){
        Log(error, "An error occured while generating the core dump");
        Trace("WriteCoreDumpInternal: Failed to open pipe to gcore");
        exit(1);
    }
    
    // read all output from gcore command
    for(i = 0; i < MAX_LINES && fgets(lineBuffer, sizeof(lineBuffer), commandPipe) != NULL; i++) {
        lineLength = strlen(lineBuffer);                                // get # of characters read

        outputBuffer[i] = (char*)malloc(sizeof(char) * lineLength);
        if(outputBuffer[i] != NULL) {
            strncpy(outputBuffer[i], lineBuffer, lineLength - 1);           // trim newline off
            outputBuffer[i][lineLength-1] = '\0';                           // append null character
        }
        else {
            Log(error, INTERNAL_ERROR);
            Trace("WriteCoreDumpInternal: failed to allocate gcore error message buffer");
            exit(-1);
        }
    }
    
    // close pipe reading from gcore
    self->Config->gcorePid = NO_PID;                // reset gcore pid so that signal handler knows we aren't dumping
    pclose(commandPipe);

    // check if gcore was able to generate the dump
    if(strstr(outputBuffer[i-1], "gcore: failed") != NULL){
        Log(error, "An error occured while generating the core dump");
                
        // log gcore message and free up memory
        for(int j = 0; j < i; j++){
            if(outputBuffer[j] != NULL){
                Log(error, "GCORE - %s", outputBuffer[j]);
                free(outputBuffer[j]);
            }
        }

        free(outputBuffer);
        exit(1);
    }

    self->Config->NumberOfDumpsCollected++; // safe to increment in crit section
    if (self->Config->NumberOfDumpsCollected >= self->Config->NumberOfDumpsToCollect) {
        SetEvent(&self->Config->evtQuit.event); // shut it down, we're done here
        rc = 1;
    }

    // validate that core dump file was generated
    if(access(coreDumpFileName, F_OK) != -1) {
        if(self->Config->nQuit){
            // if we are in a quit state from interrupt delete partially generated core dump file
            if(sprintf(command, "rm -f %s", coreDumpFileName) < 0){
                Trace("WriteCoreDumpInternal: Failed to print rm command");
                exit(-1);
            }
            
            if(system(command) < 0){
                Trace("WriteCoreDumpInternal: Failed to remove partial core dump");
                exit(-1);
            }
        }
        else{
            // log out sucessful core dump generated
            Log(info, "Core dump %d generated: %s", self->Config->NumberOfDumpsCollected, coreDumpFileName);
        }
    }

    return rc;
}

//--------------------------------------------------------------------
//
// popen2 - alternate popen that surfaces the pid of the spawned process
//
// Parameters: command (const char *) - the string containing the command to execute in the child thread
//             type (const char *) - either "r" for read or "w" for write
//             pid (pidt_t *) - out variable containing the pid of the spawned process
//
// Returns: FILE* pointing to the r or w of the pip between this thread and the spawned
//
//--------------------------------------------------------------------
FILE *popen2(const char *command, const char *type, pid_t *pid)
{
    // per man page: "...opens a process by creating a pipe, forking, and invoking the shell..."
    int pipefd[2]; // 0 -> read, 1 -> write
    pid_t childPid;

    if ((pipe(pipefd)) == -1) {
        Log(error, INTERNAL_ERROR);
        Trace("popen2: unable to open pipe");
        exit(-1);
    }

    // fork and then ensure we have the correct end of the pipe open
    if ((childPid = fork()) == -1) {
        Log(error, INTERNAL_ERROR);
        Trace("popen2: unable to fork process");
        exit(-1);
    }

    if (childPid == 0) {
        // Child
        setpgid(0,0); // give the child and descendants their own pgid so we can terminate gcore separately

        if (type[0] == 'r') {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO); // redirect stdout to write end of pipe
        } else {
            close(pipefd[1]);
            dup2(pipefd[0], STDIN_FILENO); // redirect pipe read to stdin
        }

        execl("/bin/bash", "bash", "-c", command, (char *)NULL); // won't return
        return NULL; // will never be hit; just for static analyzers
    } else {
        // parent
        setpgid(childPid, childPid); // give the child and descendants their own pgid so we can terminate gcore separately
        *pid = childPid;

        if (type[0] == 'r') {
            close(pipefd[1]);
            return fdopen(pipefd[0], "r");
        } else {
            close(pipefd[0]);
            return fdopen(pipefd[1], "w");
        }

    }
}

//--------------------------------------------------------------------
//
// sanitize - Helper function for removing all non-alphanumeric characters from process name
//
// Returns: char *
//
//--------------------------------------------------------------------
// remove all non alphanumeric characters from process name and replace with '_'
char *sanitize(char * processName)
{
    char *sanitizedProcessName = strdup(processName);
    for (int i = 0; i < strlen(sanitizedProcessName); i++)
    {
        if (!isalnum(sanitizedProcessName[i]))
        {
            sanitizedProcessName[i] = '_';
        }
    }
    return sanitizedProcessName;
}
