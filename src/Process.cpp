// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// This library reads from the /procfs pseudo filesystem
//
//--------------------------------------------------------------------
#include "Includes.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/sysctl.h>

#ifdef __APPLE__
#include <libproc.h>
#include <mach/mach_time.h>
#endif

//--------------------------------------------------------------------
//
// GetUids - Gets the process uids for the given pid
//
//--------------------------------------------------------------------
bool GetUids(pid_t pid, struct ProcessStat* proc)
{

    std::ostringstream path;
    path << "/proc/" << pid << "/status";

    std::ifstream statusFile(path.str());
    if (!statusFile.is_open())
    {
        Log(error, "Failed to open status file for pid: %d", pid);
        return false;
    }

    std::string line;
    while (std::getline(statusFile, line))
    {
        if (line.find("Uid:") == 0)
        {
            std::istringstream iss(line);
            std::string uidTag, realUid, effectiveUid, savedUid, fsUid;
            iss >> uidTag >> realUid >> effectiveUid >> savedUid >> fsUid;
            proc->real_uid = std::stoi(realUid);
            proc->effective_uid = std::stoi(effectiveUid);
            proc->saved_uid = std::stoi(savedUid);
            proc->fs_uid = std::stoi(fsUid);
            return true;
        }
    }

    return false;
}

//--------------------------------------------------------------------
//
// GetNumFileDescriptors - Gets the process stats for the given pid
//
//--------------------------------------------------------------------
bool GetNumFileDescriptors(pid_t pid, struct ProcessStat* proc)
{
#ifdef __linux__
    auto_free_dir DIR* fddir = NULL;
    struct dirent* entry = NULL;
    char procFilePath[32];

    if(sprintf(procFilePath, "/proc/%d/fdinfo", pid) < 0)
    {
        return false;
    }

    fddir = opendir(procFilePath);
    if(fddir)
    {
        proc->num_filedescriptors = 0;
        while ((entry = readdir(fddir)) != NULL)
        {
            proc->num_filedescriptors++;
        }
    }
    else
    {
        Log(error, "Failed to open %s [%s]", procFilePath, strerror(errno));
        return false;

    }

    proc->num_filedescriptors-=2;                   // Account for "." and ".."
#endif
    return true;
}

//--------------------------------------------------------------------
//
// GetProcessStat - Gets the process stats for the given pid
//
//--------------------------------------------------------------------
bool GetProcessStat(pid_t pid, struct ProcessStat *proc) {
#ifdef __linux__
    char procFilePath[32];
    char fileBuffer[1024];
    char *token;
    char *savePtr = NULL;

    auto_free_file FILE *procFile = NULL;

    // Get UID's in /proc/%d/status
    if(GetUids(pid, proc) == false)
    {
        Log(error, "Failed to get UID's");
        return false;
    }

    // Get number of file descriptors in /proc/%d/fdinfo. This directory only contains sub directories for each file descriptor.
    if(GetNumFileDescriptors(pid, proc) == false)
    {
        Log(error, "Failed to get number of file descriptors");
        return false;
    }

    // Read /proc/[pid]/stat
    if(sprintf(procFilePath, "/proc/%d/stat", pid) < 0){
        return false;
    }
    procFile = fopen(procFilePath, "r");

    if(procFile != NULL){
        if(fgets(fileBuffer, sizeof(fileBuffer), procFile) == NULL) {
            Log(error, "Failed to read from %s. Exiting...", procFilePath);
            return false;
        }
    }
    else{
        Log(error, "Failed to open %s.", procFilePath);
        return false;
    }

    // (1) process ID
    proc->pid = (pid_t)atoi(fileBuffer);

    // (3) process state
    if((savePtr = strrchr(fileBuffer, ')')) != NULL){
        savePtr += 2;     // iterate past ')' and ' ' in /proc/[pid]/stat
        proc->state = strtok_r(savePtr, " ", &savePtr)[0];
    }

    // (4) parent process ID
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - Parent PID.");
        return false;
    }

    proc->ppid = (pid_t)strtol(token, NULL, 10);

    // (5) process group ID
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - Process group ID.");
        return false;
    }

    proc->pgrp = (pid_t)strtol(token, NULL, 10);

    // (6) session ID
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - Session ID.");
        return false;
    }

    proc->session = (int)strtol(token, NULL, 10);

    // (7) controlling terminal
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - Controlling terminal.");
        return false;
    }

    proc->tty_nr = (int)strtol(token, NULL, 10);

    // (8) Foreground group ID of controlling terminal
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - Foreground group ID.");
        return false;
    }

    proc->tpgid = (pid_t)strtol(token, NULL, 10);

    // (9) Kernel flags
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - Kernel flags.");
        return false;
    }

    proc->flags = (unsigned int)strtoul(token, NULL, 10);

    // (10) minflt
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - Minflt.");
        return false;
    }

    proc->minflt = strtoul(token, NULL, 10);

    // (11) cminflt
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - cminflt.");
        return false;
    }

    proc->cminflt = strtoul(token, NULL, 10);

    // (12) majflt
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - majflt.");
        return false;
    }

    proc->majflt = strtoul(token, NULL, 10);

    // (13) cmajflt
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - cmajflt.");
        return false;
    }

    proc->cmajflt = strtoul(token, NULL, 10);

    // (14) utime
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - utime.");
        return false;
    }

    proc->utime = strtoul(token, NULL, 10);

    // (15) stime
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - stime.");
        return false;
    }

    proc->stime = strtoul(token, NULL, 10);

    // (16) cutime
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - cutime.");
        return false;
    }

    proc->cutime = strtoul(token, NULL, 10);

    // (17) cstime
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - cstime.");
        return false;
    }

    proc->cstime = strtoul(token, NULL, 10);

    // (18) priority
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - priority.");
        return false;
    }

    proc->priority = strtol(token, NULL, 10);

    // (19) nice
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - nice.");
        return false;
    }

    proc->nice = strtol(token, NULL, 10);

    // (20) num_threads
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - num_threads.");
        return false;
    }

    proc->num_threads = strtol(token, NULL, 10);

    // (21) itrealvalue
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - itrealvalue.");
        return false;
    }

    proc->itrealvalue = strtol(token, NULL, 10);

    // (22) starttime
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - starttime.");
        return false;
    }

    proc->starttime = strtoull(token, NULL, 10);

    // (23) vsize
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - vsize.");
        return false;
    }

    proc->vsize = strtoul(token, NULL, 10);

    // (24) rss
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - rss.");
        return false;
    }

    proc->rss = strtol(token, NULL, 10);

    // (25) rsslim
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - rsslim.");
        return false;
    }

    proc->rsslim = strtoul(token, NULL, 10);

    // (26) startcode
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - startcode.");
        return false;
    }

    proc->startcode = strtoul(token, NULL, 10);

    // (27) endcode
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - endcode.");
        return false;
    }

    proc->endcode = strtoul(token, NULL, 10);

    // (28) startstack
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - startstack.");
        return false;
    }

    proc->startstack = strtoul(token, NULL, 10);

    // (29) kstkesp
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - kstkesp.");
        return false;
    }

    proc->kstkesp = strtoul(token, NULL, 10);

    // (30) kstkeip
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - kstkeip.");
        return false;
    }

    proc->kstkeip = strtoul(token, NULL, 10);

    // (31) signal
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - signal.");
        return false;
    }

    proc->signal = strtoul(token, NULL, 10);

    // (32) blocked
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - blocked.");
        return false;
    }

    proc->blocked = strtoul(token, NULL, 10);

    // (33) sigignore
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - sigignore.");
        return false;
    }

    proc->sigignore = strtoul(token, NULL, 10);

    // (34) sigcatch
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - sigcatch.");
        return false;
    }

    proc->sigcatch = strtoul(token, NULL, 10);

    // (35) wchan
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - wchan.");
        return false;
    }

    proc->wchan = strtoul(token, NULL, 10);

    // (36) nswap
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - nswap.");
        return false;
    }

    proc->nswap = strtoul(token, NULL, 10);

    // (37) cnswap
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - cnswap.");
        return false;
    }

    proc->cnswap = strtoul(token, NULL, 10);

    // (38) exit_signal
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - exit_signal.");
        return false;
    }

    proc->exit_signal = (int)strtol(token, NULL, 10);

    // (39) processor
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - processor.");
        return false;
    }

    proc->processor = (int)strtol(token, NULL, 10);

    // (40) rt_priority
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - rt_priority.");
        return false;
    }

    proc->rt_priority = (unsigned int)strtoul(token, NULL, 10);

    // (41) policy
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - policy.");
        return false;
    }

    proc->policy = (unsigned int)strtoul(token, NULL, 10);

    // (42) delayacct_blkio_ticks
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - delayacct_blkio_ticks.");
        return false;
    }

    proc->delayacct_blkio_ticks = strtoull(token, NULL, 10);

    // (43) guest_time
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - guest_time.");
        return false;
    }

    proc->guest_time = strtoul(token, NULL, 10);

    // (44) cguest_time
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - cguest_time.");
        return false;
    }

    proc->cguest_time = strtol(token, NULL, 10);

    // (45) start_data
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - start_data.");
        return false;
    }

    proc->start_data = strtoul(token, NULL, 10);

    // (46) end_data
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - end_data.");
        return false;
    }

    proc->end_data = strtoul(token, NULL, 10);

    // (47) start_brk
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - start_brk.");
        return false;
    }

    proc->start_brk = strtoul(token, NULL, 10);

    // (48) arg_start
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - arg_start.");
        return false;
    }

    proc->arg_start = strtoul(token, NULL, 10);

    // (49) arg_end
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - arg_end.");
        return false;
    }

    proc->arg_end = strtoul(token, NULL, 10);

    // (50) env_start
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - env_start.");
        return false;
    }

    proc->env_start = strtoul(token, NULL, 10);

    // (52) env_end
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - env_end.");
        return false;
    }

    proc->env_end = strtoul(token, NULL, 10);

    // (53) exit_code
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessStat: failed to get token from proc/[pid]/stat - exit_code.");
        return false;
    }

    proc->exit_code = (int)strtol(token, NULL, 10);
#endif
    return true;
}

//--------------------------------------------------------------------
//
// GetProcessName - Extracts the process name from the specified
//                  commandline.
//
//--------------------------------------------------------------------
char * GetProcessNameFromCmdLine(char* cmdLine)
{
    char* retString = cmdLine;

    std::string procName = "";
    std::string inputString = cmdLine;
    size_t firstSpacePos = inputString.find(' ');
    if (firstSpacePos != std::string::npos)
    {
        procName = inputString.substr(0, firstSpacePos);
        retString = const_cast<char*>(procName.c_str());
    }

    return strdup(retString);
}

//--------------------------------------------------------------------
//
// GetProcessName - Get process name using PID provided.
//                  Returns EMPTY_PROC_NAME for null process name.
//
//--------------------------------------------------------------------
char * GetProcessName(pid_t pid)
{
#ifdef __linux__    
    char procFilePath[32];
    char fileBuffer[MAX_CMDLINE_LEN];
    int charactersRead = 0;
    int	itr = 0;
    char * stringItr;
    char * processName;
    auto_free_file FILE * procFile = NULL;

    if(sprintf(procFilePath, "/proc/%d/cmdline", pid) < 0)
    {
        return NULL;
    }

    procFile = fopen(procFilePath, "r");

    if(procFile != NULL)
    {
        if(fgets(fileBuffer, MAX_CMDLINE_LEN, procFile) == NULL)
        {
            if(strlen(fileBuffer) == 0)
            {
                Log(debug, "Empty cmdline.\n");
            }

            return NULL;
        }
    }
    else
    {
        Log(debug, "Failed to open %s.\n", procFilePath);
        return NULL;
    }


    // Extract process name
    stringItr = fileBuffer;
    charactersRead  = strlen(fileBuffer);
    for(int i = 0; i <= charactersRead; i++)
    {
        if(fileBuffer[i] == '\0')
        {
            itr = i - itr;

            // do we have the process name including filepath?
            if(strcmp(stringItr, "sudo") != 0)
            {
                processName = strrchr(stringItr, '/');	// does this process include a filepath?

                if(processName != NULL)
                {
                    return GetProcessNameFromCmdLine(processName + 1);	// +1 to not include '/' character
                }
                else
                {
                    return GetProcessNameFromCmdLine(stringItr);
                }
            }
            else
            {
                stringItr += (itr+1); 	// +1 to move past '\0'
            }
        }
    }
#elif __APPLE__
    char* pathbuf = (char*) malloc(PROC_PIDPATHINFO_MAXSIZE);
    if(pathbuf == NULL)
    {
        return NULL;
    }

    if (proc_pidpath(pid, pathbuf, PROC_PIDPATHINFO_MAXSIZE) > 0) 
    {
        // Extract the process name from the full path
        char* process_name = strrchr(pathbuf, '/');
        if (process_name) 
        {
            process_name++; 
            char* proc_copy = strdup(process_name);
            free(pathbuf);
            return proc_copy;
        }
    }

#endif

    return NULL;
}

//--------------------------------------------------------------------
//
// GetProcessPgid - Get process pgid using PID provided.
//                  Returns NO_PID on error
//
//--------------------------------------------------------------------
pid_t GetProcessPgid(pid_t pid)
{
    pid_t pgid = NO_PID;
#ifdef __linux__
    char procFilePath[32];
    char fileBuffer[1024];
    char *token;
    char *savePtr = NULL;

    auto_free_file FILE *procFile = NULL;

    // Read /proc/[pid]/stat
    if(sprintf(procFilePath, "/proc/%d/stat", pid) < 0){
        return pgid;
    }
    procFile = fopen(procFilePath, "r");

    if(procFile != NULL){
        if(fgets(fileBuffer, sizeof(fileBuffer), procFile) == NULL) {
            return pgid;
        }
    }
    else{
        Trace("GetProcessPgid: Cannot open %s to check PGID", procFilePath);
        return pgid;
    }

    // itaerate past process state
    savePtr = strrchr(fileBuffer, ')');
    savePtr += 2;   // iterate past ')' and ' ' in /proc/[pid]/stat

    // iterate past process state
    token = strtok_r(NULL, " ", &savePtr);

    // iterate past parent process ID
    token = strtok_r(NULL, " ", &savePtr);

    // grab process group ID
    token = strtok_r(NULL, " ", &savePtr);
    if(token == NULL){
        Trace("GetProcessPgid: failed to get token from proc/[pid]/stat - Process group ID.");
        return pgid;
    }

    pgid = (pid_t)strtol(token, NULL, 10);
#endif
    return pgid;
}

//--------------------------------------------------------------------
//
// LookupProcessByPid - Find process using PID provided.
//
//--------------------------------------------------------------------
bool LookupProcessByPid(pid_t pid)
{
#ifdef __linux__
    char statFilePath[32];
    auto_free_file FILE *fd = NULL;

    if(pid == NO_PID)
    {
        return false;
    }

    // check to see if pid is an actual process running1`
    if(pid != NO_PID) {
        sprintf(statFilePath, "/proc/%d/stat", pid);
    }

    fd = fopen(statFilePath, "r");
    if (fd == NULL) {
        return false;
    }
#elif __APPLE__
    // On MacOS, we can't check if a process is running by looking at /proc
    // Instead, we can use kill(pid, 0) to check if the process is running
    if (kill(pid, 0) == 0)
    {
        return true;
    }
    else
    {
        return false;
    }
#endif
    return true;
}

//--------------------------------------------------------------------
//
// LookupProcessByPgid - Find a running process using PGID provided.
//
//--------------------------------------------------------------------
bool LookupProcessByPgid(pid_t pid)
{
    bool ret = false;

    // check to see if pid is an actual process running
    if(pid != NO_PID)
    {
        struct dirent ** nameList;
        int numEntries = scandir("/proc/", &nameList, FilterForPid, alphasort);

        // evaluate all running processes
        for (int i = 0; i < numEntries; i++)
        {
            pid_t procPid;
            if(!ConvertToInt(nameList[i]->d_name, &procPid))
            {
                continue;
            }

            pid_t procPgid;

            procPgid = GetProcessPgid(procPid);

            if(procPgid != NO_PID && procPgid == pid)
            {
                ret = true;
                break;
            }
        }

        for (int i = 0; i < numEntries; i++)
        {
            free(nameList[i]);
        }
        if(numEntries!=-1)
        {
            free(nameList);
        }
    }

    // if we have ran through all the running processes then supplied PGID is invalid
    return ret;
}

//--------------------------------------------------------------------
//
// LookupProcessByName - Find a running process using name provided.
//
//--------------------------------------------------------------------
bool LookupProcessByName(const char *procName)
{
    // check to see if name is an actual process running
    bool ret = false;
    struct dirent ** nameList;
    int numEntries = scandir("/proc/", &nameList, FilterForPid, alphasort);

    // evaluate all running processes
    for (int i = 0; i < numEntries; i++)
    {
        pid_t procPid;
        if(!ConvertToInt(nameList[i]->d_name, &procPid))
        {
            continue;
        }

        char* processName = GetProcessName(procPid);

        if(processName && strcasecmp(processName, procName)==0)
        {
            free(processName);
            ret = true;
            break;
        }

        if(processName) free(processName);
    }

    for (int i = 0; i < numEntries; i++)
    {
        free(nameList[i]);
    }
    if(numEntries!=-1)
    {
        free(nameList);
    }

    // if we have ran through all the running processes then supplied PGID is invalid
    return ret;
}

//--------------------------------------------------------------------
//
// LookupProcessPidByName - Return PID of process using name provided.
//
//--------------------------------------------------------------------
pid_t LookupProcessPidByName(const char* name)
{
    // check to see if name is an actual process running
    pid_t ret = NO_PID;
    struct dirent ** nameList;
    int numEntries = scandir("/proc/", &nameList, FilterForPid, alphasort);

    // evaluate all running processes
    for (int i = 0; i < numEntries; i++) {
        pid_t procPid;
        if(!ConvertToInt(nameList[i]->d_name, &procPid))
        {
            continue;
        }

        char* procName = GetProcessName(procPid);
        if(procName && strcasecmp(name, procName)==0)
        {
            struct ProcessStat stat;
            free(procName);

            if(GetProcessStat(procPid, &stat))
            {
                ret = stat.pid;
            }

            break;
        }

        if(procName) free(procName);
    }

    for (int i = 0; i < numEntries; i++)
    {
#ifndef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-double-free"
        free(nameList[i]);          // Note: Analyzer incorrectly states that there is a double-free here which is incorrect and can be ignored.
#pragma GCC diagnostic pop
#else
        free(nameList[i]);
#endif
    }
    if(numEntries!=-1)
    {
        free(nameList);
    }

    // if we have ran through all the running processes then supplied name is not found
    return ret;
}

//--------------------------------------------------------------------
//
// GetMaximumPID - Read from kernel configs what the maximum PID value is
//
// Returns maximum PID value before processes roll over or -1 upon error.
//--------------------------------------------------------------------
int GetMaximumPID()
{
    int maxPIDs = -1;
#ifdef __linux__
    auto_free_file FILE * pidMaxFile = NULL;

    pidMaxFile = fopen(PID_MAX_KERNEL_CONFIG, "r");
    if(pidMaxFile != NULL)
    {
        if(fscanf(pidMaxFile, "%d", &maxPIDs) == EOF)
        {
            maxPIDs = -1;
        }
    }
#elif __APPLE__
    maxPIDs = INT_MAX;
#endif

    return maxPIDs;
}

//--------------------------------------------------------------------
//
// FilterForPid - Helper function for scandir to only return PIDs.
//
//--------------------------------------------------------------------
int FilterForPid(const struct dirent *entry)
{
    return IsValidNumberArg(entry->d_name);
}


//--------------------------------------------------------------------
//
// GetCpuUsage - Gets the CPU usage of a process.
//
//--------------------------------------------------------------------
#ifdef __linux__
int GetCpuUsage(ProcessStat* procStat)
{
    int cpuUsage = 0;
    struct sysinfo sysInfo;
    unsinged long totalTime;
    unsigned long elapsedTime;

    sysinfo(&sysInfo);

    // Calc CPU
    totalTime = (unsigned long)((procStat->utime + procStat->stime) / HZ);
    elapsedTime = (unsigned long)(sysInfo.uptime - (long)(procStat->starttime / HZ));
    cpuUsage = (int)(100 * ((double)totalTime / elapsedTime));

    return cpuUsage;
}
#elif __APPLE__
int GetCpuUsage(pid_t pid)
{
    int cpuUsage = 0;
    pid_t* pids = NULL;
    int ret = 0;
    struct timeval boottime;
    size_t size = sizeof(boottime);
    int mib[2] = {CTL_KERN, KERN_BOOTTIME};    

    if (sysctl(mib, 2, &boottime, &size, NULL, 0) != 0) 
    {
        return -1;
    }    

    mach_timebase_info_data_t timebaseInfo;
    kern_return_t kr = mach_timebase_info(&timebaseInfo);

    int num_pids = proc_listpids(PROC_ALL_PIDS, 0, pids, 0);
    pids = (pid_t*) malloc(num_pids*sizeof(pid_t));
    num_pids = proc_listpids(PROC_ALL_PIDS, 0, pids, num_pids*sizeof(pid_t));
    for (int i = 0; i < num_pids / sizeof(pid_t); i++) 
    {
        if(pids[i] == pid)
        {
            struct proc_taskallinfo taskInfo;
            int ret = proc_pidinfo(pid, PROC_PIDTASKALLINFO, 0, &taskInfo, sizeof(taskInfo));
            if (ret <= 0) 
            {
                Trace("[GetCpuUsage] Failed to get process information for pid: %d", pid);
                free(pids);
                return -1;
            } 
            else
            {
                unsigned long totalTime = ((taskInfo.ptinfo.pti_total_user * timebaseInfo.numer / timebaseInfo.denom)  + (taskInfo.ptinfo.pti_total_system * timebaseInfo.numer / timebaseInfo.denom)) / 1000000000;
                unsigned long elapsedTime = ((time(NULL) - boottime.tv_sec)) - (taskInfo.pbsd.pbi_start_tvsec - boottime.tv_sec);
                cpuUsage = (int)(100 * ((double)totalTime / elapsedTime));
                break;
            }           
        }   
    }

    free(pids);
    return cpuUsage;
}
#endif