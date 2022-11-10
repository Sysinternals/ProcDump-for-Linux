// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// General purpose helpers
//
//--------------------------------------------------------------------
#include "Includes.h"

//--------------------------------------------------------------------
//
// ConvertToInt - Helper to convert from a char* to int
//
//--------------------------------------------------------------------
bool ConvertToInt(const char* src, int* conv)
{
    char *end;

    long l = strtol(src, &end, 10);
    if (*end != '\0')
        return false;

    *conv = l;
    return true;
}

//--------------------------------------------------------------------
//
// CheckKernelVersion - Check to see if current kernel is 3.5+.
//
// ProcDump won't proceed if current kernel is less than 3.5.
// Returns true if >= 3.5+, returns false otherwise or error.
//--------------------------------------------------------------------
bool CheckKernelVersion()
{
    struct utsname kernelInfo;
    if(uname(&kernelInfo) == 0)
    {
        int version, patch = 0;
        if(sscanf(kernelInfo.release,"%d.%d",&version,&patch) != 2)
        {
            Log(error, "Cannot validate kernel version");
            Trace("%s",strerror(errno));
            return false;
        }

        if(version > MIN_KERNEL_VERSION) return true;
        if(version == MIN_KERNEL_VERSION && patch >= MIN_KERNEL_PATCH) return true;

    }
    else
    {
        Log(error, strerror(errno));
    }
    return false;
}


//--------------------------------------------------------------------
//
// IsValidNumberArg - quick helper function for ensuring arg is a number
//
//--------------------------------------------------------------------
bool IsValidNumberArg(const char *arg)
{
    int strLen = strlen(arg);

    for (int i = 0; i < strLen; i++) {
        if (!isdigit(arg[i]) && !isspace(arg[i])) {
            return false;
        }
    }

    return true;
}

//--------------------------------------------------------------------
//
// GetUint16 - Quick and dirty conversion from char to uint16_t
//
// Returns: uint16_t*   - if successfully converted, NULL otherwise.
//                        Caller must free upon success
//
//--------------------------------------------------------------------
uint16_t* GetUint16(char* buffer)
{
    uint16_t* dumpFileNameW = NULL;

    if(buffer!=NULL)
    {
        dumpFileNameW = malloc((strlen(buffer)+1)*sizeof(uint16_t));
        for(int i=0; i<(strlen(buffer)+1); i++)
        {
            dumpFileNameW[i] = (uint16_t) buffer[i];
        }
    }

    return dumpFileNameW;
}

//--------------------------------------------------------------------
//
// GetPath - Parses out the path from a full line read from
//           /proc/net/unix. Example line:
//
//           0000000000000000: 00000003 00000000 00000000 0001 03 20287 @/tmp/.X11-unix/X0
//
// Returns: path   - point to path if it exists, NULL otherwise.
//
//--------------------------------------------------------------------
char* GetPath(char* lineBuf)
{
    char delim[] = " ";

    // example of /proc/net/unix line:
    // 0000000000000000: 00000003 00000000 00000000 0001 03 20287 @/tmp/.X11-unix/X0
    char *ptr = strtok(lineBuf, delim);

    // Move to last column which contains the name of the file (/socket)
    for(int i=0; i<7; i++)
    {
        ptr = strtok(NULL, delim);
    }

    if(ptr!=NULL)
    {
        ptr[strlen(ptr)-1]='\0';
    }

    return ptr;
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
            dup2(pipefd[1], STDERR_FILENO); // redirect stderr to write end of pipe
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
    if(processName == NULL){
        Log(error, "NULL process name.\n");
        exit(-1);
    }

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
