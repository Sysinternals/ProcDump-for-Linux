// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// The global configuration structure and utilities header
//
//--------------------------------------------------------------------

#include "Procdump.h"
#include "ProcDumpConfiguration.h"

static sigset_t sig_set;
static pthread_t sig_thread_id;
extern pthread_mutex_t LoggerLock;
long HZ;                                                        // clock ticks per second
int MAXIMUM_CPU;                                                // maximum cpu usage percentage (# cores * 100)
struct ProcDumpConfiguration g_config;                          // backbone of the program
struct ProcDumpConfiguration * target_config;                   // list of configs for target group processes or matching names
TAILQ_HEAD(, ConfigQueueEntry) configQueueHead;
pthread_mutex_t queue_mutex;


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
// ApplyDefaults - Apply default values to configuration
//
//--------------------------------------------------------------------
void ApplyDefaults(struct ProcDumpConfiguration *self)
{
    if(self->NumberOfDumpsToCollect == -1)
    {
        self->NumberOfDumpsToCollect = DEFAULT_NUMBER_OF_DUMPS;
    }

    if(self->ThresholdSeconds == -1)
    {
        self->ThresholdSeconds = DEFAULT_DELTA_TIME;
    }

    if(self->PollingInterval == -1)
    {
        self->PollingInterval = MIN_POLLING_INTERVAL;
    }


}

//--------------------------------------------------------------------
//
// SignalThread - Thread for handling graceful Async signals (e.g., SIGINT, SIGTERM)
//
//--------------------------------------------------------------------
void *SignalThread(void *input)
{
    int sig_caught, rc;
    struct ConfigQueueEntry * item;

    if ((rc = sigwait(&sig_set, &sig_caught)) != 0) {
        Log(error, "Failed to wait on signal");
        exit(-1);
    }

    switch (sig_caught)
    {
    case SIGINT:
        // In case of CTRL-C we need to iterate over all the outstanding monitors and handle them appropriately
        pthread_mutex_lock(&queue_mutex);
        TAILQ_FOREACH(item, &configQueueHead, element)
        {
            if(!IsQuit(item->config)) SetQuit(item->config, 1);

            if(item->config->gcorePid != NO_PID) {
                Log(info, "Shutting down gcore");
                if((rc = kill(-item->config->gcorePid, SIGKILL)) != 0) {            // pass negative PID to kill entire PGRP with value of gcore PID
                    Log(error, "Failed to shutdown gcore.");
                }
            }

            // Need to make sure we detach from ptrace (if not attached it will silently fail)
            // To avoid situations where we have intercepted a signal and CTRL-C is hit, we synchronize
            // access to the signal path (in SignalMonitoringThread). Note, there is still a race but
            // acceptable since it is very unlikely to occur. We also cancel the SignalMonitorThread to
            // break it out of waitpid call.
            if(item->config->SignalNumber != -1)
            {
                for(int i=0; i<item->config->nThreads; i++)
                {
                    if(item->config->Threads[i].trigger == Signal)
                    {
                        pthread_mutex_lock(&item->config->ptrace_mutex);
                        ptrace(PTRACE_DETACH, item->config->ProcessId, 0, 0);
                        pthread_mutex_unlock(&item->config->ptrace_mutex);

                        if ((rc = pthread_cancel(item->config->Threads[i].thread)) != 0) {
                            Log(error, "An error occurred while canceling SignalMonitorThread.\n");
                            exit(-1);
                        }
                    }
                }
            }
        }

        Log(info, "Quit");
        SetQuit(&g_config, 1);                  // Make sure to signal the global config
        pthread_mutex_unlock(&queue_mutex);
        break;

        default:
        fprintf (stderr, "\nUnexpected signal %d\n", sig_caught);
        break;
    }

    pthread_exit(NULL);
}

//--------------------------------------------------------------------
//
// InitProcDump - initalize procdump
//
//--------------------------------------------------------------------
void InitProcDump()
{
    openlog("ProcDump", LOG_PID, LOG_USER);
    if(CheckKernelVersion() == false)
    {
        Log(error, "Kernel version lower than 3.5+.");
        exit(-1);
    }
    InitProcDumpConfiguration(&g_config);
    pthread_mutex_init(&LoggerLock, NULL);
    pthread_mutex_init(&queue_mutex, NULL);
}

//--------------------------------------------------------------------
//
// ExitProcDump - cleanup during exit.
//
//--------------------------------------------------------------------
void ExitProcDump()
{
    pthread_mutex_destroy(&LoggerLock);
    closelog();
    FreeProcDumpConfiguration(&g_config);
}

//--------------------------------------------------------------------
//
// InitProcDumpConfiguration - initalize a config
//
//--------------------------------------------------------------------
void InitProcDumpConfiguration(struct ProcDumpConfiguration *self)
{
    MAXIMUM_CPU = 100 * (int)sysconf(_SC_NPROCESSORS_ONLN);
    HZ = sysconf(_SC_CLK_TCK);

    sysinfo(&(self->SystemInfo));

    pthread_mutex_init(&self->ptrace_mutex, NULL);

    InitNamedEvent(&(self->evtCtrlHandlerCleanupComplete.event), true, false, "CtrlHandlerCleanupComplete");
    self->evtCtrlHandlerCleanupComplete.type = EVENT;

    InitNamedEvent(&(self->evtBannerPrinted.event), true, false, "BannerPrinted");
    self->evtBannerPrinted.type = EVENT;

    InitNamedEvent(&(self->evtConfigurationPrinted.event), true, false, "ConfigurationPrinted");
    self->evtConfigurationPrinted.type = EVENT;

    InitNamedEvent(&(self->evtDebugThreadInitialized.event), true, false, "DebugThreadInitialized");
    self->evtDebugThreadInitialized.type = EVENT;

    InitNamedEvent(&(self->evtQuit.event), true, false, "Quit");
    self->evtQuit.type = EVENT;

    InitNamedEvent(&(self->evtStartMonitoring.event), true, false, "StartMonitoring");
    self->evtStartMonitoring.type = EVENT;

    sem_init(&(self->semAvailableDumpSlots.semaphore), 0, 1);
    self->semAvailableDumpSlots.type = SEMAPHORE;

    // Additional initialization
    self->ProcessId =                   NO_PID;
    self->bProcessGroup =               false;
    self->ProcessGroup =                NO_PID;
    self->NumberOfDumpsCollected =      0;
    self->NumberOfDumpsToCollect =      -1;
    self->CpuThreshold =                -1;
    self->bCpuTriggerBelowValue =       false;
    self->MemoryThreshold =             -1;
    self->ThreadThreshold =             -1;
    self->FileDescriptorThreshold =     -1;
    self->SignalNumber =                -1;
    self->ThresholdSeconds =            -1;
    self->bMemoryTriggerBelowValue =    false;
    self->bTimerThreshold =             false;
    self->WaitingForProcessName =       false;
    self->DiagnosticsLoggingEnabled =   false;
    self->gcorePid =                    NO_PID;
    self->PollingInterval =             -1;
    self->CoreDumpPath =                NULL;
    self->CoreDumpName =                NULL;
    self->nQuit =                       0;
}


//--------------------------------------------------------------------
//
// FreeProcDumpConfiguration - ensure destruction of config and contents
//
//--------------------------------------------------------------------
void FreeProcDumpConfiguration(struct ProcDumpConfiguration *self)
{
    DestroyEvent(&(self->evtCtrlHandlerCleanupComplete.event));
    DestroyEvent(&(self->evtBannerPrinted.event));
    DestroyEvent(&(self->evtConfigurationPrinted.event));
    DestroyEvent(&(self->evtDebugThreadInitialized.event));
    DestroyEvent(&(self->evtQuit.event));
    DestroyEvent(&(self->evtStartMonitoring.event));

    pthread_mutex_destroy(&self->ptrace_mutex);
    sem_destroy(&(self->semAvailableDumpSlots.semaphore));

    if(self->WaitingForProcessName){
        free(self->ProcessName);
    }

    free(self->CoreDumpPath);
    free(self->CoreDumpName);
}


//--------------------------------------------------------------------
//
// CopyProcDumpConfiguration - deep copy of Procdump Config struct
//
//--------------------------------------------------------------------
struct ProcDumpConfiguration * CopyProcDumpConfiguration(struct ProcDumpConfiguration *self)
{
    struct ProcDumpConfiguration * copy = (struct ProcDumpConfiguration*)malloc(sizeof(struct ProcDumpConfiguration));

    if(copy != NULL) {
        // Init new struct
        InitProcDumpConfiguration(copy);

        // copy target data we need from original config
        copy->ProcessId = self->ProcessId;
        copy->bProcessGroup = self->bProcessGroup;
        copy->ProcessGroup = self->ProcessGroup;
        copy->ProcessName = self->ProcessName == NULL ? NULL : strdup(self->ProcessName);

        // copy runtime values from original config
        copy->NumberOfDumpsCollecting = self->NumberOfDumpsCollecting;
        copy->NumberOfDumpsCollected = self->NumberOfDumpsCollected;
        copy->bTerminated = self->bTerminated;

        // copy trigger behavior from original config
        copy->bTriggerThenSnoozeCPU = self->bTriggerThenSnoozeCPU;
        copy->bTriggerThenSnoozeMemory = self->bTriggerThenSnoozeMemory;
        copy->bTriggerThenSnoozeTimer = self->bTriggerThenSnoozeTimer;

        // copy options from original config
        copy->CpuThreshold = self->CpuThreshold;
        copy->bCpuTriggerBelowValue = self->bCpuTriggerBelowValue;
        copy->MemoryThreshold = self->MemoryThreshold;
        copy->bMemoryTriggerBelowValue = self->bMemoryTriggerBelowValue;
        copy->ThresholdSeconds = self->ThresholdSeconds;
        copy->bTimerThreshold = self->bTimerThreshold;
        copy->NumberOfDumpsToCollect = self->NumberOfDumpsToCollect;
        copy->WaitingForProcessName = self->WaitingForProcessName;
        copy->DiagnosticsLoggingEnabled = self->DiagnosticsLoggingEnabled;
        copy->ThreadThreshold = self->ThreadThreshold;
        copy->FileDescriptorThreshold = self->FileDescriptorThreshold;
        copy->SignalNumber = self->SignalNumber;
        copy->PollingInterval = self->PollingInterval;
        copy->CoreDumpPath = self->CoreDumpPath == NULL ? NULL : strdup(self->CoreDumpPath);
        copy->CoreDumpName = self->CoreDumpName == NULL ? NULL : strdup(self->CoreDumpName);

        return copy;
    }
    else {
        Trace("Failed to alloc memory for Procdump config copy");
        return NULL;
    }
}


//--------------------------------------------------------------------
//
// GetOptions - Unpack command line inputs
//
//--------------------------------------------------------------------
int GetOptions(struct ProcDumpConfiguration *self, int argc, char *argv[])
{
    bool bProcessSpecified = false;

    if (argc < 2) {
        Trace("GetOptions: Invalid number of command line arguments.");
        return PrintUsage();
    }

    for( int i = 1; i < argc; i++ )
    {
        if (0 == strcasecmp( argv[i], "/?" ) || 0 == strcasecmp( argv[i], "-?" ))
        {
            return PrintUsage();
        }
        else if ( 0 == strcasecmp( argv[i], "/c" ) ||
                   0 == strcasecmp( argv[i], "-c" ) ||
                   0 == strcasecmp( argv[i], "/cl" ) ||
                   0 == strcasecmp( argv[i], "-cl" ))
        {
            if( i+1 >= argc || self->CpuThreshold != -1 ) return PrintUsage();
            if(!ConvertToInt(argv[i+1], &self->CpuThreshold)) return PrintUsage();

            if(self->CpuThreshold < 0)
            {
                Log(error, "Invalid CPU threshold count specified.");
                return PrintUsage();
            }

            if( 0 == strcasecmp( argv[i], "/cl" ) || 0 == strcasecmp( argv[i], "-cl"))
            {
                self->bCpuTriggerBelowValue = true;
            }

            i++;
        }
        else if( 0 == strcasecmp( argv[i], "/m" ) ||
                    0 == strcasecmp( argv[i], "-m" ) ||
                    0 == strcasecmp( argv[i], "/ml" ) ||
                    0 == strcasecmp( argv[i], "-ml" ))
        {
            if( i+1 >= argc || self->MemoryThreshold != -1 ) return PrintUsage();
            if(!ConvertToInt(argv[i+1], &self->MemoryThreshold)) return PrintUsage();

            if(self->MemoryThreshold < 0)
            {
                Log(error, "Invalid memory threshold count specified.");
                return PrintUsage();
            }

            if( 0 == strcasecmp( argv[i], "/ml" ) || 0 == strcasecmp( argv[i], "-ml" ))
            {
                self->bMemoryTriggerBelowValue = true;
            }

            i++;
        }
        else if( 0 == strcasecmp( argv[i], "/tc" ) ||
                    0 == strcasecmp( argv[i], "-tc" ))
        {
            if( i+1 >= argc || self->ThreadThreshold != -1 ) return PrintUsage();
            if(!ConvertToInt(argv[i+1], &self->ThreadThreshold)) return PrintUsage();
            if(self->ThreadThreshold < 0)
            {
                Log(error, "Invalid thread threshold count specified.");
                return PrintUsage();
            }

            i++;
        }
        else if( 0 == strcasecmp( argv[i], "/fc" ) ||
                    0 == strcasecmp( argv[i], "-fc" ))
        {
            if( i+1 >= argc || self->FileDescriptorThreshold != -1 ) return PrintUsage();
            if(!ConvertToInt(argv[i+1], &self->FileDescriptorThreshold)) return PrintUsage();
            if(self->FileDescriptorThreshold < 0)
            {
                Log(error, "Invalid file descriptor threshold count specified.");
                return PrintUsage();
            }

            i++;
        }
        else if( 0 == strcasecmp( argv[i], "/sig" ) ||
                    0 == strcasecmp( argv[i], "-sig" ))
        {
            if( i+1 >= argc || self->SignalNumber != -1 ) return PrintUsage();
            if(!ConvertToInt(argv[i+1], &self->SignalNumber)) return PrintUsage();
            if(self->SignalNumber < 0)
            {
                Log(error, "Invalid signal specified.");
                return PrintUsage();
            }

            i++;
        }
        else if( 0 == strcasecmp( argv[i], "/pf" ) ||
                    0 == strcasecmp( argv[i], "-pf" ))
        {
            if( i+1 >= argc || self->PollingInterval != -1 ) return PrintUsage();
            if(!ConvertToInt(argv[i+1], &self->PollingInterval)) return PrintUsage();
            if(self->PollingInterval < 0)
            {
                Log(error, "Invalid polling inverval specified.");
                return PrintUsage();
            }

            i++;
        }
        else if( 0 == strcasecmp( argv[i], "/n" ) ||
                    0 == strcasecmp( argv[i], "-n" ))
        {
            if( i+1 >= argc || self->NumberOfDumpsToCollect != -1 ) return PrintUsage();
            if(!ConvertToInt(argv[i+1], &self->NumberOfDumpsToCollect)) return PrintUsage();
            if(self->NumberOfDumpsToCollect < 0)
            {
                Log(error, "Invalid number of dumps specified.");
                return PrintUsage();
            }

            i++;
        }
        else if( 0 == strcasecmp( argv[i], "/s" ) ||
                    0 == strcasecmp( argv[i], "-s" ))
        {
            if( i+1 >= argc || self->ThresholdSeconds != -1 ) return PrintUsage();
            if(!ConvertToInt(argv[i+1], &self->ThresholdSeconds)) return PrintUsage();
            if(self->ThresholdSeconds < 0)
            {
                Log(error, "Invalid seconds specified.");
                return PrintUsage();
            }

            i++;
        }
        else if( 0 == strcasecmp( argv[i], "/log" ) ||
                    0 == strcasecmp( argv[i], "-log" ))
        {
            self->DiagnosticsLoggingEnabled = true;
        }
        else if( 0 == strcasecmp( argv[i], "/o" ) ||
                    0 == strcasecmp( argv[i], "-o" ))
        {
            self->bOverwriteExisting = true;
        }
        else if( 0 == strcasecmp( argv[i], "/w" ) ||
                    0 == strcasecmp( argv[i], "-w" ))
        {
            self->WaitingForProcessName = true;
        }
        else if( 0 == strcasecmp( argv[i], "/pgid" ) ||
                    0 == strcasecmp( argv[i], "-pgid" ))
        {
            self->bProcessGroup = true;
        }
        else
        {
            // Process targets
            int j;
            if( bProcessSpecified && self->CoreDumpPath )
            {
                return PrintUsage();
            } else if(!bProcessSpecified)
            {
                bProcessSpecified = true;
                bool isPid = true;

                for( j = 0; j < (int) strlen( argv[i]); j++ )
                {

                    if( !isdigit( argv[i][j]) )
                    {

                        isPid = false;
                        break;
                    }
                }
                if( !isPid )
                {

                    self->ProcessName = strdup(argv[i]);

                } else
                {
                    if(self->bProcessGroup)
                    {
                        if( !sscanf( argv[i], "%d", &self->ProcessGroup ))
                        {

                            return PrintUsage();
                        }
                    }
                    else
                    {
                        if( !sscanf( argv[i], "%d", &self->ProcessId ))
                        {

                            return PrintUsage();
                        }
                    }
                }

            } else if(!self->CoreDumpPath)
            {
                char *tempOutputPath = NULL;
                tempOutputPath = strdup(argv[i]);
                struct stat statbuf;

                // Check if the user provided an existing directory or a path
                // ending in a '/'. In this case, use the default naming
                // convention but place the files in the given directory.
                if ((stat(tempOutputPath, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) ||
                        tempOutputPath[strlen(tempOutputPath)-1] == '/') {
                    self->CoreDumpPath = tempOutputPath;
                    self->CoreDumpName = NULL;
                } else {
                    self->CoreDumpPath = strdup(dirname(tempOutputPath));
                    free(tempOutputPath);
                    tempOutputPath = strdup(argv[i]);
                    self->CoreDumpName = strdup(basename(tempOutputPath));
                    free(tempOutputPath);
                }

                // Check if the path portion of the output format is valid
                if (stat(self->CoreDumpPath, &statbuf) < 0 || !S_ISDIR(statbuf.st_mode)) {
                    Log(error, "Invalid directory (\"%s\") provided for core dump output.",
                        self->CoreDumpPath);
                    return PrintUsage();
                }
            }
        }
    }

    //
    // Validate multi arguments
    //

    // If no path was provided, assume the current directory
    if (self->CoreDumpPath == NULL) {
        self->CoreDumpPath = strdup(".");
    }

    // Wait
    if((self->WaitingForProcessName && self->ProcessId != NO_PID))
    {
        Log(error, "The wait option requires the process be specified by name.");
        return PrintUsage();
    }

    // If number of dumps to collect is set, but there is no other criteria, enable Timer here...
    if ((self->CpuThreshold == -1) &&
        (self->MemoryThreshold == -1) &&
        (self->ThreadThreshold == -1) &&
        (self->FileDescriptorThreshold == -1))
    {
        self->bTimerThreshold = true;
    }

    // Signal trigger can only be specified alone
    if(self->SignalNumber != -1)
    {
        if(self->CpuThreshold != -1 || self->ThreadThreshold != -1 || self->FileDescriptorThreshold != -1 || self->MemoryThreshold != -1)
        {
            Log(error, "Signal trigger must be the only trigger specified.");
            return PrintUsage();
        }
        if(self->PollingInterval != -1)
        {
            Log(error, "Polling interval has no meaning during signal monitoring.");
            return PrintUsage();
        }

        // Again, we cant have another trigger (in this case timer) kicking off another dump generation since we will already
        // be attached via ptrace.
        self->bTimerThreshold = false;
    }

    // If we are monitoring multiple process, setting dump name doesn't make sense (path is OK)
    if ((self->bProcessGroup || self->WaitingForProcessName) && self->CoreDumpName)
    {
        Log(error, "Setting core dump name in multi process monitoring is invalid (path is ok).");
        return PrintUsage();
    }

    // Apply default values for any config values that were not specified by user
    ApplyDefaults(self);

    Trace("GetOpts and initial Configuration finished");

    return 0;
}


//--------------------------------------------------------------------
//
// LookupProcessByPid - Find process using PID provided.
//
//--------------------------------------------------------------------
bool LookupProcessByPid(struct ProcDumpConfiguration *self)
{
    char statFilePath[32];

    if(self->ProcessId == NO_PID)
    {
        return false;
    }

    // check to see if pid is an actual process running1`
    if(self->ProcessId != NO_PID) {
        sprintf(statFilePath, "/proc/%d/stat", self->ProcessId);
    }

    FILE *fd = fopen(statFilePath, "r");
    if (fd == NULL) {
        return false;
    }

    // close file pointer this is a valid process
    fclose(fd);
    return true;
}


//--------------------------------------------------------------------
//
// FilterForPid - Helper function for scandir to only return PIDs.
//
//--------------------------------------------------------------------
static int FilterForPid(const struct dirent *entry)
{
    return IsValidNumberArg(entry->d_name);
}


//--------------------------------------------------------------------
//
// LookupProcessByPgid - Find a running process using PGID provided.
//
//--------------------------------------------------------------------
bool LookupProcessByPgid(struct ProcDumpConfiguration *self)
{
    // check to see if pid is an actual process running
    if(self->ProcessGroup != NO_PID && self->bProcessGroup) {
        struct dirent ** nameList;
        int numEntries = scandir("/proc/", &nameList, FilterForPid, alphasort);

        // evaluate all running processes
        for (int i = 0; i < numEntries; i++) {
            pid_t procPid;
            if(!ConvertToInt(nameList[i]->d_name, &procPid)) return false;
            pid_t procPgid;

            procPgid = GetProcessPgid(procPid);

            if(procPgid != NO_PID && procPgid == self->ProcessGroup)
                return true;
        }
    }

    // if we have ran through all the running processes then supplied PGID is invalid
    return false;
}


//--------------------------------------------------------------------
//
// LookupProcessByName - Find a running process using name provided.
//
//--------------------------------------------------------------------
bool LookupProcessByName(struct ProcDumpConfiguration *self)
{
    // check to see if name is an actual process running
    struct dirent ** nameList;
    int numEntries = scandir("/proc/", &nameList, FilterForPid, alphasort);

    // evaluate all running processes
    for (int i = 0; i < numEntries; i++) {
        pid_t procPid;
        if(!ConvertToInt(nameList[i]->d_name, &procPid)) return false;

        char* processName = GetProcessName(procPid);

        if(processName && strcasecmp(processName, self->ProcessName)==0)
        {
            free(processName);
            return true;
        }

        if(processName) free(processName);
    }

    // if we have ran through all the running processes then supplied PGID is invalid
    return false;
}

//--------------------------------------------------------------------
//
// LookupProcessPidByName - Return PID of process using name provided.
//
//--------------------------------------------------------------------
pid_t LookupProcessPidByName(const char* name)
{
    // check to see if name is an actual process running
    struct dirent ** nameList;
    int numEntries = scandir("/proc/", &nameList, FilterForPid, alphasort);

    // evaluate all running processes
    for (int i = 0; i < numEntries; i++) {
        pid_t procPid;
        if(!ConvertToInt(nameList[i]->d_name, &procPid)) return false;

        char* procName = GetProcessName(procPid);
        if(procName && strcasecmp(name, procName)==0)
        {
            struct ProcessStat stat;
            free(procName);

            if(!GetProcessStat(procPid, &stat))
            {
                return NO_PID;
            }

            return stat.pid;
        }
    }

    // if we have ran through all the running processes then supplied name is not found
    return NO_PID;
}


//--------------------------------------------------------------------
//
// StartMonitor
// Creates the monitoring threads and begins the monitor based on
// the configuration passed in.
//
//--------------------------------------------------------------------
int StartMonitor(struct ProcDumpConfiguration* monitorConfig)
{
    int ret = 0;

    if(CreateTriggerThreads(monitorConfig) != 0)
    {
        Log(error, INTERNAL_ERROR);
        Trace("MonitorProcesses: failed to create trigger threads.");
        ret = -1;
    }

    if(BeginMonitoring(monitorConfig) == false)
    {
        Log(error, INTERNAL_ERROR);
        Trace("MonitorProcesses: failed to start monitoring.");
        ret = -1;
    }

    Log(info, "Starting monitor for process %s (%d)", monitorConfig->ProcessName, monitorConfig->ProcessId);

    return ret;
}

//--------------------------------------------------------------------
//
// MonitorProcesses
// MonitorProcess is the starting point of where the monitors get
// created. It uses a list to store all the monitors that are active.
// All monitors must go on this list as there are other places (for
// example, SignalThread) that relies on all active monitors to be part
// of the list. Any access to this list must be protected by queue_mutex.
//
//--------------------------------------------------------------------
void MonitorProcesses(struct ProcDumpConfiguration *self)
{
    if (self->WaitingForProcessName)    Log(info, "Waiting for processes '%s' to launch\n", self->ProcessName);
    if (self->bProcessGroup == true)    Log(info, "Monitoring processes of PGID '%d'\n", self->ProcessGroup);

    // allocate list of configs for process monitoring
    TAILQ_INIT(&configQueueHead);
    int numMonitoredProcesses = 0;
    struct ConfigQueueEntry * item;

    // create binary map to track processes we have already tracked and closed
    int maxPid = GetMaximumPID();
    if(maxPid < 0)
    {
        Log(error, INTERNAL_ERROR);
        Trace("Unable to get MAX_PID value\n");
        return;
    }

    struct MonitoredProcessMapEntry* monitoredProcessMap = (struct MonitoredProcessMapEntry*) calloc(maxPid, sizeof(struct MonitoredProcessMapEntry));
    if(!monitoredProcessMap)
    {
        Log(error, INTERNAL_ERROR);
        Trace("CreateTriggerThreads: failed to allocate memory for monitorProcessMap.");
        ExitProcDump();
    }

    // Create a signal handler thread where we handle shutdown as a result of SIGINT.
    // Note: We only create ONE per instance of procdump rather than per monitor.
    if((pthread_create(&sig_thread_id, NULL, SignalThread, (void *)self))!= 0)
    {
        Log(error, INTERNAL_ERROR);
        Trace("CreateTriggerThreads: failed to create SignalThread.");
        free(monitoredProcessMap);
        ExitProcDump();
    }

    Log(info, "\n\nPress Ctrl-C to end monitoring without terminating the process(es).\n");

    if(!self->WaitingForProcessName && !self->bProcessGroup)
    {
        //
        // Monitoring single process (-p)
        //

        //
        // Make sure target process exists
        //

        // If we have a process name find it to make sure it exists
        if(self->ProcessName)
        {
            if(!LookupProcessByName(self))
            {
                Log(error, "No process matching the specified name (%s) can be found.", self->ProcessName);
                return;
            }

            // Set the process ID so the monitor can target.
            self->ProcessId = LookupProcessPidByName(g_config.ProcessName);
        }
        else if (self->ProcessId != NO_PID && !LookupProcessByPid(self))
        {
            Log(error, "No process matching the specified PID (%d) can be found.", self->ProcessId);
            return;
        }

        self->ProcessName = GetProcessName(self->ProcessId);

        item = (struct ConfigQueueEntry*)malloc(sizeof(struct ConfigQueueEntry));
        item->config = CopyProcDumpConfiguration(self);

        if(item->config == NULL)
        {
            Log(error, INTERNAL_ERROR);
            Trace("MonitorProcesses: failed to alloc struct for process.");
            free(monitoredProcessMap);
            ExitProcDump();
        }

        // insert config into queue
        pthread_mutex_lock(&queue_mutex);
        TAILQ_INSERT_HEAD(&configQueueHead, item, element);
        monitoredProcessMap[item->config->ProcessId].active = true;
        pthread_mutex_unlock(&queue_mutex);

        // print config here
        PrintConfiguration(self);

        if(StartMonitor(self)!=0)
        {
            Log(error, INTERNAL_ERROR);
            Trace("MonitorProcesses: Failed to start the monitor.");
            free(monitoredProcessMap);
            ExitProcDump();
        }

        WaitForAllMonitoringThreadsToTerminate(self);
        Log(info, "Stopping monitor for process %s (%d)", self->ProcessName, self->ProcessId);
        WaitForSignalThreadToTerminate(self);

        pthread_mutex_lock(&queue_mutex);
        TAILQ_REMOVE(&configQueueHead, item, element);
        monitoredProcessMap[item->config->ProcessId].active = false;
        pthread_mutex_unlock(&queue_mutex);
        FreeProcDumpConfiguration(item->config);
        free(item);
    }
    else
    {
        // print config here
        PrintConfiguration(self);

        do
        {
            // Multi process monitoring

            // If we are monitoring for PGID, validate the root process exists
            if(self->bProcessGroup && !LookupProcessByPgid(self)) {
                Log(error, "No process matching the specified PGID can be found.");
                PrintUsage();
                return;
            }

            // Iterate over all running processes
            struct dirent ** nameList;
            int numEntries = scandir("/proc/", &nameList, FilterForPid, alphasort);
            for (int i = 0; i < numEntries; i++)
            {
                pid_t procPid;
                if(!ConvertToInt(nameList[i]->d_name, &procPid)) return;

                if(self->bProcessGroup)
                {
                    // We are monitoring a process group (-g)
                    pid_t pgid = GetProcessPgid(procPid);
                    if(pgid != NO_PID && pgid == self->ProcessGroup)
                    {
                        struct ProcessStat procStat;
                        bool ret = GetProcessStat(procPid, &procStat);

                        // Note: To solve the PID reuse case, we uniquely identify an entry via {PID}{starttime}
                        if(ret && (monitoredProcessMap[procPid].active == false || monitoredProcessMap[procPid].starttime != procStat.starttime))
                        {
                            // allocate for new queue entry
                            item = (struct ConfigQueueEntry*)malloc(sizeof(struct ConfigQueueEntry));
                            item->config = CopyProcDumpConfiguration(self);

                            if(item->config == NULL)
                            {
                                Log(error, INTERNAL_ERROR);
                                Trace("MonitorProcesses: failed to alloc struct for process.");
                                free(monitoredProcessMap);
                                ExitProcDump();
                            }

                            // populate fields for this target
                            item->config->ProcessId = procPid;
                            item->config->ProcessName = GetProcessName(procPid);

                            // insert config into queue
                            pthread_mutex_lock(&queue_mutex);
                            TAILQ_INSERT_HEAD(&configQueueHead, item, element);
                            monitoredProcessMap[item->config->ProcessId].active = true;
                            monitoredProcessMap[item->config->ProcessId].starttime = procStat.starttime;
                            pthread_mutex_unlock(&queue_mutex);

                            if(StartMonitor(item->config)!=0)
                            {
                                Log(error, INTERNAL_ERROR);
                                Trace("MonitorProcesses: Failed to start the monitor.");
                                free(monitoredProcessMap);
                                ExitProcDump();
                            }

                            numMonitoredProcesses++;
                        }
                    }
                }
                else if(self->WaitingForProcessName)
                {
                    // We are monitoring for a process name (-w)
                    char *nameForPid = GetProcessName(procPid);

                    // check to see if process name matches target
                    if (nameForPid && strcmp(nameForPid, self->ProcessName) == 0)
                    {
                        struct ProcessStat procStat;
                        bool ret = GetProcessStat(procPid, &procStat);

                        // Note: To solve the PID reuse case, we uniquely identify an entry via {PID}{starttime}
                        if(ret && (monitoredProcessMap[procPid].active == false || monitoredProcessMap[procPid].starttime != procStat.starttime))
                        {
                            // allocate for new queue entry
                            item = (struct ConfigQueueEntry*)malloc(sizeof(struct ConfigQueueEntry));
                            item->config = CopyProcDumpConfiguration(self);

                            if(item->config == NULL)
                            {
                                Log(error, INTERNAL_ERROR);
                                Trace("MonitorProcesses: failed to alloc struct for named process.");
                                free(monitoredProcessMap);
                                ExitProcDump();
                            }

                            // populate fields for this target
                            item->config->ProcessId = procPid;
                            item->config->ProcessName = strdup(nameForPid);

                            // insert config into queue
                            pthread_mutex_lock(&queue_mutex);
                            TAILQ_INSERT_HEAD(&configQueueHead, item, element);
                            monitoredProcessMap[item->config->ProcessId].active = true;
                            monitoredProcessMap[item->config->ProcessId].starttime = procStat.starttime;
                            pthread_mutex_unlock(&queue_mutex);

                            if(StartMonitor(item->config)!=0)
                            {
                                Log(error, INTERNAL_ERROR);
                                Trace("MonitorProcesses: Failed to start the monitor.");
                                free(monitoredProcessMap);
                                ExitProcDump();
                            }

                            numMonitoredProcesses++;
                        }
                    }
                }
            }

            // clean up namelist
            for (int i = 0; i < numEntries; i++)
            {
                free(nameList[i]);
            }
            free(nameList);

            // cleanup process configs for child processes that have exited or for monitors that have captured N dumps
            pthread_mutex_lock(&queue_mutex);

            // TALQ_FOREACH does not support changing the list while iterating.
            // We use a seperate delete list instead.
            int count = 0;
            TAILQ_FOREACH(item, &configQueueHead, element)
            {
                count++;
            }

            struct ConfigQueueEntry** deleteList = (struct ConfigQueueEntry**) malloc(count*(sizeof(struct ConfigQueueEntry*)));
            if(deleteList == NULL)
            {
                Log(error, INTERNAL_ERROR);
                Trace("WriteCoreDumpInternal: failed to allocate memory for deleteList");
                ExitProcDump();
            }

            count = 0;

            // Iterate over the queue and store the items to delete
            TAILQ_FOREACH(item, &configQueueHead, element)
            {
                // The conditions under which we stop monitoring a process are:
                // 1. Process has been terminated
                // 2. The monitoring thread has exited
                // 3. The monitor has collected the required number of dumps
                if(item->config->bTerminated || item->config->nQuit || item->config->NumberOfDumpsCollected == item->config->NumberOfDumpsToCollect)
                {
                    Log(info, "Stopping monitors for process: %s (%d)", item->config->ProcessName, item->config->ProcessId);
                    WaitForAllMonitoringThreadsToTerminate(item->config);

                    deleteList[count++] = item;

                    numMonitoredProcesses--;
                }
            }

            // Iterate over the delete list and actually delete the items from the queue
            for(int i=0; i<count; i++)
            {
                // free config entry
                FreeProcDumpConfiguration(deleteList[i]->config);
                TAILQ_REMOVE(&configQueueHead, deleteList[i], element);
                free(deleteList[i]);
            }
            free(deleteList);
            pthread_mutex_unlock(&queue_mutex);

            // Exit if we are monitoring PGID and there are no more processes to monitor.
            // If we are monitoring for processes based on a process name we keep monitoring
            if(numMonitoredProcesses == 0 && self->WaitingForProcessName == false)
            {
                break;
            }

            // Wait for the polling interval the user specified before we check again
            sleep(g_config.PollingInterval / 1000);

        // We keep iterating while we have processes to monitor (in case of -g <pgid>) or if process name has
        // been specified (-w) in which case we keep monitoring until CTRL-C or finally if we have a quit signal.
        } while ((numMonitoredProcesses >= 0 || self->WaitingForProcessName == true) && !IsQuit(&g_config));

        // cleanup monitoring queue
        pthread_mutex_lock(&queue_mutex);
        int count = 0;
        TAILQ_FOREACH(item, &configQueueHead, element)
        {
            count++;
        }

        struct ConfigQueueEntry** deleteList = (struct ConfigQueueEntry**) malloc(count*(sizeof(struct ConfigQueueEntry*)));
        if(deleteList == NULL)
        {
            Log(error, INTERNAL_ERROR);
            Trace("WriteCoreDumpInternal: failed to allocate memory for deleteList");
            free(monitoredProcessMap);
            ExitProcDump();
        }

        count = 0;

        TAILQ_FOREACH(item, &configQueueHead, element)
        {
            SetQuit(item->config, 1);
            WaitForAllMonitoringThreadsToTerminate(item->config);

            deleteList[count++] = item;
        }

        // Iterate over the delete list and actually delete the items from the queue
        for(int i=0; i<count; i++)
        {
            // free config entry
            FreeProcDumpConfiguration(deleteList[i]->config);
            TAILQ_REMOVE(&configQueueHead, deleteList[i], element);
            free(deleteList[i]);
        }
        free(deleteList);
        pthread_mutex_unlock(&queue_mutex);

        free(target_config);

    }
}

//--------------------------------------------------------------------
//
// GetProcessPgid - Get process pgid using PID provided.
//                  Returns NO_PID on error
//
//--------------------------------------------------------------------
pid_t GetProcessPgid(pid_t pid){
	pid_t pgid = NO_PID;

    char procFilePath[32];
    char fileBuffer[1024];
    char *token;
    char *savePtr = NULL;

    FILE *procFile = NULL;

    // Read /proc/[pid]/stat
    if(sprintf(procFilePath, "/proc/%d/stat", pid) < 0){
        return pgid;
    }
    procFile = fopen(procFilePath, "r");

    if(procFile != NULL){
        if(fgets(fileBuffer, sizeof(fileBuffer), procFile) == NULL) {
            fclose(procFile);
            return pgid;
        }

        // close file after reading this iteration of stats
        fclose(procFile);
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

    return pgid;
}

//--------------------------------------------------------------------
//
// GetProcessName - Get process name using PID provided.
//                  Returns EMPTY_PROC_NAME for null process name.
//
//--------------------------------------------------------------------
char * GetProcessName(pid_t pid){
	char procFilePath[32];
	char fileBuffer[MAX_CMDLINE_LEN];
	int charactersRead = 0;
	int	itr = 0;
	char * stringItr;
	char * processName;
	FILE * procFile;


    if(sprintf(procFilePath, "/proc/%d/cmdline", pid) < 0) {
		return NULL;
	}

	procFile = fopen(procFilePath, "r");

	if(procFile != NULL) {
		if(fgets(fileBuffer, MAX_CMDLINE_LEN, procFile) == NULL) {
            fclose(procFile);

            if(strlen(fileBuffer) == 0) {
                Log(debug, "Empty cmdline.\n");
            }
            else{
			}
			return NULL;
		}

		// close file
		fclose(procFile);
	}
	else {
		Log(debug, "Failed to open %s.\n", procFilePath);
		return NULL;
	}


	// Extract process name
	stringItr = fileBuffer;
	charactersRead  = strlen(fileBuffer);
	for(int i = 0; i <= charactersRead; i++){
		if(fileBuffer[i] == '\0'){
			itr = i - itr;

			if(strcmp(stringItr, "sudo") != 0){		// do we have the process name including filepath?
				processName = strrchr(stringItr, '/');	// does this process include a filepath?

				if(processName != NULL){
					return strdup(processName + 1);	// +1 to not include '/' character
				}
				else{
					return strdup(stringItr);
				}
			}
			else{
				stringItr += (itr+1); 	// +1 to move past '\0'
			}
		}
	}

	return NULL;
}

//--------------------------------------------------------------------
//
// CreateTriggerThreads - Create each of the threads that will be running as a trigger
//
//--------------------------------------------------------------------
int CreateTriggerThreads(struct ProcDumpConfiguration *self)
{
    int rc = 0;
    self->nThreads = 0;
    bool tooManyTriggers = false;

    if((rc=sigemptyset (&sig_set)) < 0)
    {
        Trace("CreateTriggerThreads: sigemptyset failed.");
        return rc;
    }
    if((rc=sigaddset (&sig_set, SIGINT)) < 0)
    {
        Trace("CreateTriggerThreads: sigaddset failed.");
        return rc;
    }
    if((rc=sigaddset (&sig_set, SIGTERM)) < 0)
    {
        Trace("CreateTriggerThreads: sigaddset failed.");
        return rc;
    }

    if((rc = pthread_sigmask (SIG_BLOCK, &sig_set, NULL)) != 0)
    {
        Trace("CreateTriggerThreads: pthread_sigmask failed.");
        return rc;
    }

    // create threads
    if (self->CpuThreshold != -1) {
        if (self->nThreads < MAX_TRIGGERS) {
            if ((rc = pthread_create(&self->Threads[self->nThreads].thread, NULL, CpuMonitoringThread, (void *)self)) != 0) {
                Trace("CreateTriggerThreads: failed to create CpuThread.");
                return rc;
            }

            self->Threads[self->nThreads].trigger = Processor;
            self->nThreads++;

        } else
            tooManyTriggers = true;
    }

    if (self->MemoryThreshold != -1 && !tooManyTriggers) {
        if (self->nThreads < MAX_TRIGGERS) {
            if ((rc = pthread_create(&self->Threads[self->nThreads].thread, NULL, CommitMonitoringThread, (void *)self)) != 0) {
                Trace("CreateTriggerThreads: failed to create CommitThread.");
                return rc;
            }

            self->Threads[self->nThreads].trigger = Commit;
            self->nThreads++;

        } else
            tooManyTriggers = true;
    }

    if (self->ThreadThreshold != -1 && !tooManyTriggers) {
        if (self->nThreads < MAX_TRIGGERS) {
            if ((rc = pthread_create(&self->Threads[self->nThreads].thread, NULL, ThreadCountMonitoringThread, (void *)self)) != 0) {
                Trace("CreateTriggerThreads: failed to create ThreadThread.");
                return rc;
            }

            self->Threads[self->nThreads].trigger = ThreadCount;
            self->nThreads++;

        } else
            tooManyTriggers = true;
    }

    if (self->FileDescriptorThreshold != -1 && !tooManyTriggers) {
        if (self->nThreads < MAX_TRIGGERS) {
            if ((rc = pthread_create(&self->Threads[self->nThreads].thread, NULL, FileDescriptorCountMonitoringThread, (void *)self)) != 0) {
                Trace("CreateTriggerThreads: failed to create FileDescriptorThread.");
                return rc;
            }

            self->Threads[self->nThreads].trigger = FileDescriptorCount;
            self->nThreads++;
        } else
            tooManyTriggers = true;
    }

    if (self->SignalNumber != -1 && !tooManyTriggers) {
        if ((rc = pthread_create(&self->Threads[self->nThreads].thread, NULL, SignalMonitoringThread, (void *)self)) != 0) {
            Trace("CreateTriggerThreads: failed to create SignalMonitoringThread.");
            return rc;
        }

        self->Threads[self->nThreads].trigger = Signal;
        self->nThreads++;
    }

    if (self->bTimerThreshold && !tooManyTriggers) {
        if (self->nThreads < MAX_TRIGGERS) {
            if ((rc = pthread_create(&self->Threads[self->nThreads].thread, NULL, TimerThread, (void *)self)) != 0) {
                Trace("CreateTriggerThreads: failed to create TimerThread.");
                return rc;
            }

            self->Threads[self->nThreads].trigger = Timer;
            self->nThreads++;
        } else
            tooManyTriggers = true;
    }

    if (tooManyTriggers)
    {
        Log(error, "Too many triggers.  ProcDump only supports up to %d triggers.", MAX_TRIGGERS);
        return -1;
    }

    return 0;
}


//--------------------------------------------------------------------
//
// WaitForQuit - Wait for Quit Event or just timeout
//
//      Timed wait with awareness of quit event
//
// Returns: WAIT_OBJECT_0   - Quit triggered
//          WAIT_TIMEOUT    - Timeout
//          WAIT_ABANDONED  - At dump limit or terminated
//
//--------------------------------------------------------------------
int WaitForQuit(struct ProcDumpConfiguration *self, int milliseconds)
{
    if (!ContinueMonitoring(self)) {
        return WAIT_ABANDONED;
    }

    int wait = WaitForSingleObject(&self->evtQuit, milliseconds);

    if ((wait == WAIT_TIMEOUT) && !ContinueMonitoring(self)) {
        return WAIT_ABANDONED;
    }

    return wait;
}


//--------------------------------------------------------------------
//
// WaitForQuitOrEvent - Wait for Quit Event, an Event, or just timeout
//
//      Use to wait for dumps to complete, yet be aware of quit or finished events
//
// Returns: WAIT_OBJECT_0   - Quit triggered
//          WAIT_OBJECT_0+1 - Event triggered
//          WAIT_TIMEOUT    - Timeout
//          WAIT_ABANDONED  - (Abandonded) At dump limit or terminated
//
//--------------------------------------------------------------------
int WaitForQuitOrEvent(struct ProcDumpConfiguration *self, struct Handle *handle, int milliseconds)
{
    struct Handle *waits[2];
    waits[0] = &self->evtQuit;
    waits[1] = handle;

    if (!ContinueMonitoring(self)) {
        return WAIT_ABANDONED;
    }

    int wait = WaitForMultipleObjects(2, waits, false, milliseconds);
    if ((wait == WAIT_TIMEOUT) && !ContinueMonitoring(self)) {
        return WAIT_ABANDONED;
    }

    if ((wait == WAIT_OBJECT_0) && !ContinueMonitoring(self)) {
        return WAIT_ABANDONED;
    }

    return wait;
}


//--------------------------------------------------------------------
//
// WaitForAllMonitoringThreadsToTerminate - Wait for all trigger threads to terminate
//
//--------------------------------------------------------------------
int WaitForAllMonitoringThreadsToTerminate(struct ProcDumpConfiguration *self)
{
    int rc = 0;

    // Wait for the other monitoring threads
    for (int i = 0; i < self->nThreads; i++) {
        if ((rc = pthread_join(self->Threads[i].thread, NULL)) != 0) {
            Log(error, "An error occurred while joining threads\n");
            exit(-1);
        }
    }

    return rc;
}

//--------------------------------------------------------------------
//
// WaitForSignalThreadToTerminate - Wait for signal handler thread to terminate
//
//--------------------------------------------------------------------
int WaitForSignalThreadToTerminate(struct ProcDumpConfiguration *self)
{
    int rc = 0;

    // Cancel the signal handling thread.
    // We dont care about the return since the signal thread might already be gone.
    pthread_cancel(sig_thread_id);

    // Wait for signal handling thread to complete
    if ((rc = pthread_join(sig_thread_id, NULL)) != 0) {
        Log(error, "An error occurred while joining SignalThread.\n");
        exit(-1);
    }

    return rc;
}

//--------------------------------------------------------------------
//
// IsQuit - A check on the underlying value of whether we should quit
//
//--------------------------------------------------------------------
bool IsQuit(struct ProcDumpConfiguration *self)
{
    return (self->nQuit != 0);
}


//--------------------------------------------------------------------
//
// SetQuit - Sets the quit value and signals the event
//
//--------------------------------------------------------------------
int SetQuit(struct ProcDumpConfiguration *self, int quit)
{
    self->nQuit = quit;
    SetEvent(&self->evtQuit.event);

    return self->nQuit;
}


//--------------------------------------------------------------------
//
// PrintConfiguration - Prints the current configuration to the command line
//
//--------------------------------------------------------------------
bool PrintConfiguration(struct ProcDumpConfiguration *self)
{
    if (WaitForSingleObject(&self->evtConfigurationPrinted,0) == WAIT_TIMEOUT) {
        if(self->SignalNumber != -1) {
            printf("** NOTE ** Signal triggers use PTRACE which will impact the performance of the target process\n\n");
        }

        if (self->bProcessGroup) {
            printf("%-40s%d\n", "Process Group:", self->ProcessGroup);
        }
        else if (self->WaitingForProcessName) {
            printf("%-40s%s\n", "Process Name:", self->ProcessName);
        }
        else {
            printf("%-40s%s (%d)\n", "Process:", self->ProcessName, self->ProcessId);
        }

        // CPU
        if (self->CpuThreshold != -1) {
            if (self->bCpuTriggerBelowValue) {
                printf("%-40s< %d%%\n", "CPU Threshold:", self->CpuThreshold);
            } else {
                printf("%-40s>= %d%%\n", "CPU Threshold:", self->CpuThreshold);
            }
        } else {
            printf("%-40s%s\n", "CPU Threshold:", "n/a");
        }

        // Memory
        if (self->MemoryThreshold != -1) {
            if (self->bMemoryTriggerBelowValue) {
                printf("%-40s<%d MB\n", "Commit Threshold:", self->MemoryThreshold);
            } else {
                printf("%-40s>=%d MB\n", "Commit Threshold:", self->MemoryThreshold);
            }
        } else {
            printf("%-40s%s\n", "Commit Threshold:", "n/a");
        }

        // Thread
        if (self->ThreadThreshold != -1) {
            printf("%-40s%d\n", "Thread Threshold:", self->ThreadThreshold);
        }
        else {
            printf("%-40s%s\n", "Thread Threshold:", "n/a");
        }

        // File descriptor
        if (self->FileDescriptorThreshold != -1) {
            printf("%-40s%d\n", "File Descriptor Threshold:", self->FileDescriptorThreshold);
        }
        else {
            printf("%-40s%s\n", "File Descriptor Threshold:", "n/a");
        }

        // Signal
        if (self->SignalNumber != -1) {
            printf("%-40s%d\n", "Signal:", self->SignalNumber);
        }
        else {
            printf("%-40s%s\n", "Signal:", "n/a");
        }

        // Polling inverval
        printf("%-40s%d\n", "Polling Interval (ms):", self->PollingInterval);

        // time
        printf("%-40s%d\n", "Threshold (s):", self->ThresholdSeconds);

        // number of dumps and others
        printf("%-40s%d\n", "Number of Dumps:", self->NumberOfDumpsToCollect);

        // Output directory and filename
        printf("%-40s%s\n", "Output directory:", self->CoreDumpPath);
        if (self->CoreDumpName != NULL) {
            printf("%-40s%s_<counter>.<pid>\n", "Custom name for core dumps:", self->CoreDumpName);
        }

        SetEvent(&self->evtConfigurationPrinted.event);
        return true;
    }
    return false;
}


//--------------------------------------------------------------------
//
// ContinueMonitoring - Should we keep monitoring or should we clean up our thread
//
//--------------------------------------------------------------------
bool ContinueMonitoring(struct ProcDumpConfiguration *self)
{
    // Have we reached the dump limit?
    if (self->NumberOfDumpsCollected >= self->NumberOfDumpsToCollect) {
        return false;
    }

    // Do we already know the process is terminated?
    if (self->bTerminated) {
        return false;
    }

    // check if any process are running with PGID
    if(self->bProcessGroup && kill(-1 * self->ProcessGroup, 0)) {
        self->bTerminated = true;
        return false;
    }

    // Let's check to make sure the process is still alive then
    // note: kill([pid], 0) doesn't send a signal but does perform error checking
    //       therefore, if it returns 0, the process is still alive, -1 means it errored out
    if (self->ProcessId != NO_PID && kill(self->ProcessId, 0)) {
        self->bTerminated = true;
        Log(warn, "Target process %d is no longer alive", self->ProcessId);
        return false;
    }

    // Otherwise, keep going!
    return true;
}

//--------------------------------------------------------------------
//
// BeginMonitoring - Sync up monitoring threads
//
//--------------------------------------------------------------------
bool BeginMonitoring(struct ProcDumpConfiguration *self)
{
    return SetEvent(&(self->evtStartMonitoring.event));
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
// GetMaximumPID - Read from kernel configs what the maximum PID value is
//
// Returns maximum PID value before processes roll over or -1 upon error.
//--------------------------------------------------------------------
int GetMaximumPID()
{
    FILE * pidMaxFile;
    int maxPIDs;

    pidMaxFile = fopen(PID_MAX_KERNEL_CONFIG, "r");

    if(pidMaxFile != NULL){
        fscanf(pidMaxFile, "%d", &maxPIDs);
        fclose(pidMaxFile);

        return maxPIDs;
    }
    else {
        return -1;
    }
}

//--------------------------------------------------------------------
//
// PrintBanner - Not re-entrant safe banner printer. Function must be called before trigger threads start.
//
//--------------------------------------------------------------------
void PrintBanner()
{
    printf("\nProcDump v1.3 - Sysinternals process dump utility\n");
    printf("Copyright (C) 2022 Microsoft Corporation. All rights reserved. Licensed under the MIT license.\n");
    printf("Mark Russinovich, Mario Hewardt, John Salem, Javid Habibi\n");
    printf("Sysinternals - www.sysinternals.com\n\n");

    printf("Monitors a process and writes a core dump file when the process exceeds the\n");
    printf("specified criteria.\n\n");
}


//--------------------------------------------------------------------
//
// PrintUsage - Print usage
//
//--------------------------------------------------------------------
int PrintUsage()
{
    printf("\nCapture Usage: \n");
    printf("   procdump [-n Count]\n");
    printf("            [-s Seconds]\n");
    printf("            [-c|-cl CPU_Usage]\n");
    printf("            [-m|-ml Commit_Usage]\n");
    printf("            [-tc Thread_Threshold]\n");
    printf("            [-fc FileDescriptor_Threshold]\n");
    printf("            [-sig Signal_Number]\n");
    printf("            [-pf Polling_Frequency]\n");
    printf("            [-o]\n");
    printf("            [-log]\n");
    printf("            {\n");
    printf("             {{[-w] Process_Name | [-pgid] PID} [Dump_File | Dump_Folder]}\n");
    printf("            }\n");
    printf("\n");
    printf("Options:\n");
    printf("   -n      Number of dumps to write before exiting.\n");
    printf("   -s      Consecutive seconds before dump is written (default is 10).\n");
    printf("   -c      CPU threshold above which to create a dump of the process.\n");
    printf("   -cl     CPU threshold below which to create a dump of the process.\n");
    printf("   -m      Memory commit threshold in MB at which to create a dump.\n");
    printf("   -ml     Trigger when memory commit drops below specified MB value.\n");
    printf("   -tc     Thread count threshold above which to create a dump of the process.\n");
    printf("   -fc     File descriptor count threshold above which to create a dump of the process.\n");
    printf("   -sig    Signal number to intercept to create a dump of the process.\n");
    printf("   -pf     Polling frequency.\n");
    printf("   -o      Overwrite existing dump file.\n");
    printf("   -log    Writes extended ProcDump tracing to syslog.\n");
    printf("   -w      Wait for the specified process to launch if it's not running.\n");
    printf("   -pgid   Process ID specified refers to a process group ID.\n");

    return -1;
}
