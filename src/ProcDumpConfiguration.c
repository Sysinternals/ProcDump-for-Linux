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
static pthread_t sig_monitor_thread_id;
extern pthread_mutex_t LoggerLock;
long HZ;                                                        // clock ticks per second
int MAXIMUM_CPU;                                                // maximum cpu usage percentage (# cores * 100)
struct ProcDumpConfiguration g_config;                          // backbone of the program
struct ProcDumpConfiguration * target_config;                   // list of configs for target group processes or matching names
TAILQ_HEAD(, ConfigQueueEntry) configQueueHead;
pthread_mutex_t ptrace_mutex;
pthread_mutex_t queue_mutex;

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
                pthread_mutex_lock(&ptrace_mutex);
                ptrace(PTRACE_DETACH, item->config->ProcessId, 0, 0);
                pthread_mutex_unlock(&ptrace_mutex);

                if ((rc = pthread_cancel(sig_monitor_thread_id)) != 0) {
                    Log(error, "An error occurred while canceling SignalMonitorThread.\n");
                    exit(-1);
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
    pthread_mutex_init(&ptrace_mutex, NULL);
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
    self->ProcessGroupId =              NO_PID;
    self->NumberOfDumpsCollected =      0;
    self->NumberOfDumpsToCollect =      DEFAULT_NUMBER_OF_DUMPS;
    self->CpuUpperThreshold =           -1;
    self->CpuLowerThreshold =           -1;    
    self->MemoryThreshold =             -1;
    self->ThreadThreshold =             -1;
    self->FileDescriptorThreshold =     -1;
    self->SignalNumber =                -1;
    self->ThresholdSeconds =            DEFAULT_DELTA_TIME;
    self->bMemoryTriggerBelowValue =    false;
    self->bTimerThreshold =             false;
    self->WaitingForProcessName =       false;
    self->DiagnosticsLoggingEnabled =   false;
    self->gcorePid =                    NO_PID;
    self->PollingInterval =             MIN_POLLING_INTERVAL;
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
        copy->ProcessGroupId = self->ProcessGroupId;
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
        copy->CpuUpperThreshold = self->CpuUpperThreshold;
        copy->CpuLowerThreshold = self->CpuLowerThreshold;
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
    if (argc < 2) {
        Trace("GetOptions: Invalid number of command line arguments.");
        return PrintUsage(self);
    }

    // parse arguments
	int next_option;
    int option_index = 0;
    const char* short_options = ":p:+pg:g:C:c:M:m:n:s:w:T:F:G:I:o:dh";
    const struct option long_options[] = {
    	{ "pid",                       required_argument,  NULL,           'p' },
        { "pgid",                      required_argument,  NULL,           'g' },
    	{ "cpu",                       required_argument,  NULL,           'C' },
    	{ "lower-cpu",                 required_argument,  NULL,           'c' },
    	{ "memory",                    required_argument,  NULL,           'M' },
    	{ "lower-mem",                 required_argument,  NULL,           'm' },
        { "number-of-dumps",           required_argument,  NULL,           'n' },
        { "time-between-dumps",        required_argument,  NULL,           's' },
        { "wait",                      required_argument,  NULL,           'w' },
        { "threads",                   required_argument,  NULL,           'T' },
        { "filedescriptors",           required_argument,  NULL,           'F' },
        { "signal",                    required_argument,  NULL,           'G' },
        { "pollinginterval",           required_argument,  NULL,           'I' },                        
        { "output-path",               required_argument,  NULL,           'o' },
        { "diag",                      no_argument,        NULL,           'd' },
        { "help",                      no_argument,        NULL,           'h' },
        { NULL,                        0,                  NULL,            0  }
    };

    char *tempOutputPath = NULL;
    struct stat statbuf;

    // start parsing command line arguments
    while ((next_option = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
        switch (next_option) {
            case 'p':
                if (self->ProcessGroupId != NO_PID){
                    Log(error, "Cannot specify both PID and PGID target.");
                    return PrintUsage(self);
                }
                self->ProcessId = (pid_t)atoi(optarg);
                if (!LookupProcessByPid(self)) {
                    Log(error, "No process matching the specified PID can be found.");
                    Log(error, "Try elevating the command prompt (i.e., `sudo procdump ...`)");
                    return PrintUsage(self);
                }
                break;
            
            case 'g':
                if (self->ProcessId != NO_PID){
                    Log(error, "Cannot specify both PID and PGID target.");
                    return PrintUsage(self);
                }
                self->ProcessGroupId = (pid_t)atoi(optarg);
                if(self->ProcessGroupId <= 0) {
                    Log(error, "Invalid Process Group ID Specified");
                    return PrintUsage(self);
                }
                if(!LookupProcessByPgid(self)) {
                    Log(error, "No process matching the specified PGID can be found.");
                    Log(error, "Try elevating the command prompt (i.e., `sudo procdump ...`)");
                    return PrintUsage(self);
                }
                break;

            case 'C':
                if (self->CpuUpperThreshold != -1 || !IsValidNumberArg(optarg) ||
                    (self->CpuUpperThreshold = atoi(optarg)) < 0 || self->CpuUpperThreshold > MAXIMUM_CPU) {
                    Log(error, "Invalid CPU threshold specified1.");
                    return PrintUsage(self);
                }
                break;

            case 'I':
                if (!IsValidNumberArg(optarg) || (self->PollingInterval = atoi(optarg)) < 0 || self->PollingInterval < MIN_POLLING_INTERVAL) {
                    Log(error, "Invalid polling interval specified (minimum %d).", MIN_POLLING_INTERVAL);
                    return PrintUsage(self);
                }
                break;

            case 'T':
                if (self->ThreadThreshold != -1 || !IsValidNumberArg(optarg) ||
                    (self->ThreadThreshold = atoi(optarg)) < 0 ) {
                    Log(error, "Invalid threads threshold specified.");
                    return PrintUsage(self);
                }
                break;

            case 'F':
                if (self->FileDescriptorThreshold != -1 || !IsValidNumberArg(optarg) ||
                    (self->FileDescriptorThreshold = atoi(optarg)) < 0 ) {
                    Log(error, "Invalid file descriptor threshold specified.");
                    return PrintUsage(self);
                }
                break;

            case 'G':
                if (self->SignalNumber != -1 || !IsValidNumberArg(optarg) ||
                    (self->SignalNumber = atoi(optarg)) < 0 ) {
                    Log(error, "Invalid signal specified.");
                    return PrintUsage(self);
                }
                break;                

            case 'c':
                if (self->CpuLowerThreshold != -1 || !IsValidNumberArg(optarg) ||
                    (self->CpuLowerThreshold = atoi(optarg)) < 0 || self->CpuLowerThreshold > MAXIMUM_CPU) {
                    Log(error, "Invalid CPU threshold specified2.");
                    return PrintUsage(self);
                }
                break;

            case 'M':
                if (self->MemoryThreshold != -1 || 
                    !IsValidNumberArg(optarg) ||
                    (self->MemoryThreshold = atoi(optarg)) < 0) {
                    Log(error, "Invalid memory threshold specified.");
                    return PrintUsage(self);
                }
                break;

            case 'm':
                if (self->MemoryThreshold != -1 || 
                    !IsValidNumberArg(optarg) ||
                    (self->MemoryThreshold = atoi(optarg)) < 0) {
                    Log(error, "Invalid memory threshold specified.");
                    return PrintUsage(self);
                }            
                self->bMemoryTriggerBelowValue = true;
                break;

            case 'n':
                if (!IsValidNumberArg(optarg) ||
                    (self->NumberOfDumpsToCollect = atoi(optarg)) < 0) {
                    Log(error, "Invalid dumps threshold specified.");
                    return PrintUsage(self);
                }                
                break;

            case 's':
                if (!IsValidNumberArg(optarg) ||
                    (self->ThresholdSeconds = atoi(optarg)) == 0) {
                    Log(error, "Invalid time threshold specified.");
                    return PrintUsage(self);
                }
                break;

            case 'w':
                self->WaitingForProcessName = true;
                self->ProcessName = strdup(optarg);
                break;

            case 'o':
                tempOutputPath = strdup(optarg);

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
                    tempOutputPath = strdup(optarg);
                    self->CoreDumpName = strdup(basename(tempOutputPath));
                    free(tempOutputPath);
                }

                // Check if the path portion of the output format is valid
                if (stat(self->CoreDumpPath, &statbuf) < 0 || !S_ISDIR(statbuf.st_mode)) {
                    Log(error, "Invalid directory (\"%s\") provided for core dump output.",
                        self->CoreDumpPath);
                    return PrintUsage(self);
                }
                break;

            case 'd':
                self->DiagnosticsLoggingEnabled = true;
                break;

            case 'h':
                return PrintUsage(self);

            default:
                Log(error, "Invalid switch specified");
                return PrintUsage(self);
        }
    }

    // If no path was provided, assume the current directory
    if (self->CoreDumpPath == NULL) {
        self->CoreDumpPath = strdup(".");
    }

    // Check for multi-arg situations
    // if number of dumps is set, but no thresholds, just go on timer
    if (self->NumberOfDumpsToCollect != -1 &&
        self->MemoryThreshold == -1 &&
        self->CpuUpperThreshold == -1 &&
        self->CpuLowerThreshold == -1 &&
        self->ThreadThreshold == -1 &&
        self->FileDescriptorThreshold == -1) {
            self->bTimerThreshold = true;
        }


    // If signal dump is specified, it can be the only trigger that is used.
    // Otherwise we might run into a situation where the other triggers invoke
    // gcore while the target is being ptraced due to signal trigger.
    // Interval has no meaning during signal monitoring.
    // 
    if(self->SignalNumber != -1) 
    {
        if(self->CpuUpperThreshold != -1 || self->CpuLowerThreshold != -1 || self->ThreadThreshold != -1 || self->FileDescriptorThreshold != -1 || self->MemoryThreshold != -1)
        {
            Log(error, "Signal trigger must be the only trigger specified.");
            return PrintUsage(self);            
        }
        if(self->PollingInterval != MIN_POLLING_INTERVAL)
        {
            Log(error, "Polling interval has no meaning during signal monitoring.");
            return PrintUsage(self);            
        }

        // Again, we cant have another trigger (in this case timer) kicking off another dump generation since we will already
        // be attached via ptrace. 
        self->bTimerThreshold = false;
    }


    if(self->ProcessId == NO_PID && self->ProcessGroupId == NO_PID && !self->WaitingForProcessName){
        Log(error, "A valid PID or process name must be specified");
        return PrintUsage(self);
    }

    if(self->ProcessId != NO_PID && self->WaitingForProcessName){
        Log(error, "Please only specify one of -p or -w");
        return PrintUsage(self);
    }

    if(self->ProcessId != NO_PID && self->ProcessGroupId != NO_PID){
        Log(error, "Please only specify one of -p or -g");
        return PrintUsage(self);
    }

    if(self->ProcessGroupId != NO_PID && self->WaitingForProcessName){
        Log(error, "Please only specify one of -g or -w");
        return PrintUsage(self);
    }

    if(!self->WaitingForProcessName && self->ProcessId != NO_PID) {
        self->ProcessName = GetProcessName(self->ProcessId);
    }

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
    // check to see if pid is an actual process running1`
    if(self->ProcessGroupId != NO_PID) {
        struct dirent ** nameList;
        int numEntries = scandir("/proc/", &nameList, FilterForPid, alphasort);
        
        // evaluate all running processes
        for (int i = 0; i < numEntries; i++) {
            pid_t procPid = atoi(nameList[i]->d_name);
            pid_t procPgid;
            
            procPgid = GetProcessPgid(procPid);

            if(procPgid != NO_PID && procPgid == self->ProcessGroupId) 
                return true;
        }
    }

    // if we have ran through all the running processes then supplied PGID is invalid
    return false;
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
    if (self->WaitingForProcessName)    Log(info, "Waiting for processes '%s' to launch...", self->ProcessName);
    if (self->ProcessGroupId != NO_PID) Log(info, "Monitoring processes of PGID '%d'", self->ProcessGroupId);

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

    bool monitoredProcessMap[maxPid];

    // Create a signal handler thread where we handle shutdown as a result of SIGINT.
    // Note: We only create ONE per instance of procdump rather than per monitor. 
    if((pthread_create(&sig_thread_id, NULL, SignalThread, (void *)self))!= 0)
    {
        Log(error, INTERNAL_ERROR);        
        Trace("CreateTriggerThreads: failed to create SignalThread.");
        ExitProcDump();
    }


    if(!g_config.WaitingForProcessName && g_config.ProcessGroupId == NO_PID)
    {
        // Monitoring single process (-p)
        item = (struct ConfigQueueEntry*)malloc(sizeof(struct ConfigQueueEntry));
        item->config = CopyProcDumpConfiguration(self);

        if(item->config == NULL) 
        {
            Log(error, INTERNAL_ERROR);
            Trace("MonitorProcesses: failed to alloc struct for process.");
            ExitProcDump();
        }

        // insert config into queue
        pthread_mutex_lock(&queue_mutex);
        TAILQ_INSERT_HEAD(&configQueueHead, item, element);
        monitoredProcessMap[item->config->ProcessId] = true;
        pthread_mutex_unlock(&queue_mutex);

        if(CreateTriggerThreads(&g_config) != 0) 
        {
            Log(error, INTERNAL_ERROR);
            Trace("MonitorProcesses: failed to create trigger threads.");
            ExitProcDump();
        }

        if(BeginMonitoring(&g_config) == false) 
        {
            Log(error, INTERNAL_ERROR);
            Trace("MonitorProcesses: failed to start monitoring.");
            ExitProcDump();
        }

        Log(info, "Starting monitor for process %s (%d)", g_config.ProcessName, g_config.ProcessId);
        WaitForAllMonitoringThreadsToTerminate(&g_config);
        Log(info, "Stopping monitor for process %s (%d)", g_config.ProcessName, g_config.ProcessId);
        WaitForSignalThreadToTerminate(&g_config);

        pthread_mutex_lock(&queue_mutex);
        TAILQ_REMOVE(&configQueueHead, item, element);
        monitoredProcessMap[item->config->ProcessId] = false;
        pthread_mutex_unlock(&queue_mutex);        
        FreeProcDumpConfiguration(item->config);
        free(item);
    }
    else
    {
        do
        {
            // Multi process monitoring

            // Iterate over all running processes
            struct dirent ** nameList;
            int numEntries = scandir("/proc/", &nameList, FilterForPid, alphasort);

            for (int i = 0; i < numEntries; i++)
            {
                pid_t procPid = atoi(nameList[i]->d_name);

                if(self->ProcessGroupId != NO_PID)
                {
                    // We are monitoring a process group (-g)
                    pid_t pgid = GetProcessPgid(procPid);
                    if(pgid != NO_PID && pgid == self->ProcessGroupId)
                    {
                        if(monitoredProcessMap[procPid] == false) 
                        {
                            // allocate for new queue entry
                            item = (struct ConfigQueueEntry*)malloc(sizeof(struct ConfigQueueEntry));
                            item->config = CopyProcDumpConfiguration(self);

                            if(item->config == NULL) 
                            {
                                Log(error, INTERNAL_ERROR);
                                Trace("MonitorProcesses: failed to alloc struct for process.");
                                ExitProcDump();
                            }

                            // populate fields for this target
                            item->config->ProcessId = procPid;
                            item->config->ProcessName = GetProcessName(procPid);

                            // insert config into queue
                            pthread_mutex_lock(&queue_mutex);
                            TAILQ_INSERT_HEAD(&configQueueHead, item, element);
                            monitoredProcessMap[item->config->ProcessId] = true;
                            pthread_mutex_unlock(&queue_mutex);                            

                            // launch new monitor
                            if(CreateTriggerThreads(item->config) != 0) 
                            {
                                Log(error, INTERNAL_ERROR);
                                Trace("MonitorProcesses: failed to create trigger threads.");
                                ExitProcDump();
                            }

                            if(BeginMonitoring(item->config) == false) 
                            {
                                Log(error, INTERNAL_ERROR);
                                Trace("MonitorProcesses: failed to start monitoring.");
                                ExitProcDump();
                            }
                            else 
                            {
                                if(item->config->ProcessGroupId == procPid)
                                {
                                    Log(info, "Starting monitor for root process %s (%d)", item->config->ProcessName, item->config->ProcessId);
                                }
                                else
                                {
                                    Log(info, "Starting monitor for process %s (%d)", item->config->ProcessName, item->config->ProcessId);
                                }
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
                    if (strcmp(nameForPid, self->ProcessName) == 0) 
                    {
                        if(monitoredProcessMap[procPid] == false) 
                        {
                            // allocate for new queue entry
                            item = (struct ConfigQueueEntry*)malloc(sizeof(struct ConfigQueueEntry));
                            item->config = CopyProcDumpConfiguration(self);

                            if(item->config == NULL) 
                            {
                                Log(error, INTERNAL_ERROR);
                                Trace("MonitorProcesses: failed to alloc struct for named process.");
                                ExitProcDump();
                            }

                            // populate fields for this target
                            item->config->ProcessId = procPid;
                            item->config->ProcessName = strdup(nameForPid);

                            // insert config into queue
                            pthread_mutex_lock(&queue_mutex);
                            TAILQ_INSERT_HEAD(&configQueueHead, item, element);
                            monitoredProcessMap[item->config->ProcessId] = true;
                            pthread_mutex_unlock(&queue_mutex);

                            // launch new monitor
                            if(CreateTriggerThreads(item->config) != 0) 
                            {
                                Log(error, INTERNAL_ERROR);
                                Trace("MonitorProcesses: failed to create trigger threads.");
                                ExitProcDump();
                            }

                            if(BeginMonitoring(item->config) == false) 
                            {
                                Log(error, INTERNAL_ERROR);
                                Trace("MonitorProcesses: failed to start monitoring.");
                                ExitProcDump();
                            }
                            else 
                            {
                                Log(info, "Starting monitor for process %s (%d)", item->config->ProcessName, item->config->ProcessId);
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
                // is the current config in a quit state?
                if(item->config->bTerminated || item->config->NumberOfDumpsCollected == item->config->NumberOfDumpsToCollect) 
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
		return EMPTY_PROC_NAME;
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
			return EMPTY_PROC_NAME;
		}

		// close file
		fclose(procFile);
	}
	else {
		Log(debug, "Failed to open %s.\n", procFilePath);
		return EMPTY_PROC_NAME;
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

	return EMPTY_PROC_NAME;
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
    if (self->CpuUpperThreshold != -1 || self->CpuLowerThreshold != -1) {
        if (self->nThreads < MAX_TRIGGERS) {
            if ((rc = pthread_create(&self->Threads[self->nThreads++], NULL, CpuMonitoringThread, (void *)self)) != 0) {
                Trace("CreateTriggerThreads: failed to create CpuThread.");
                return rc;
            }
        } else
            tooManyTriggers = true;
    }

    if (self->MemoryThreshold != -1 && !tooManyTriggers) {
        if (self->nThreads < MAX_TRIGGERS) {
            if ((rc = pthread_create(&self->Threads[self->nThreads++], NULL, CommitMonitoringThread, (void *)self)) != 0) {
                Trace("CreateTriggerThreads: failed to create CommitThread.");            
                return rc;
            }
        } else
            tooManyTriggers = true;
    }

    if (self->ThreadThreshold != -1 && !tooManyTriggers) {
        if (self->nThreads < MAX_TRIGGERS) {
            if ((rc = pthread_create(&self->Threads[self->nThreads++], NULL, ThreadCountMonitoringThread, (void *)self)) != 0) {
                Trace("CreateTriggerThreads: failed to create ThreadThread.");            
                return rc;
            }
        } else
            tooManyTriggers = true;
    }

    if (self->FileDescriptorThreshold != -1 && !tooManyTriggers) {
        if (self->nThreads < MAX_TRIGGERS) {
            if ((rc = pthread_create(&self->Threads[self->nThreads++], NULL, FileDescriptorCountMonitoringThread, (void *)self)) != 0) {
                Trace("CreateTriggerThreads: failed to create FileDescriptorThread.");            
                return rc;
            }
        } else
            tooManyTriggers = true;
    }

    if (self->SignalNumber != -1 && !tooManyTriggers) {
        if ((rc = pthread_create(&sig_monitor_thread_id, NULL, SignalMonitoringThread, (void *)self)) != 0) {
            Trace("CreateTriggerThreads: failed to create SignalMonitoringThread.");            
            return rc;
        }
    }

    if (self->bTimerThreshold && !tooManyTriggers) {
        if (self->nThreads < MAX_TRIGGERS) {
            if ((rc = pthread_create(&self->Threads[self->nThreads++], NULL, TimerThread, (void *)self)) != 0) {
                Trace("CreateTriggerThreads: failed to create TimerThread.");
                return rc;
            }
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
    if(self->SignalNumber != -1)
    {
        if ((rc = pthread_join(sig_monitor_thread_id, NULL)) != 0) {
            Log(error, "An error occurred while joining SignalMonitorThread.\n");
            exit(-1);
        }
    }
    else
    {
        // Wait for the other monitoring threads
        for (int i = 0; i < self->nThreads; i++) {
            if ((rc = pthread_join(self->Threads[i], NULL)) != 0) {
                Log(error, "An error occurred while joining threads\n");
                exit(-1);
            }
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

        if (self->ProcessGroupId != NO_PID) {
            printf("%-40s%d\n", "Process Group:", self->ProcessGroupId);
        }
        else if (self->WaitingForProcessName) {
            printf("%-40s%s\n", "Process Name:", self->ProcessName);
        }
        else {
            printf("%-40s%s (%d)\n", "Process:", self->ProcessName, self->ProcessId);
        }

        // CPU
        if (self->CpuLowerThreshold != -1) {
            printf("%-40s%s%d\n", "CPU (less than) Threshold:", "< ", self->CpuLowerThreshold);
        } 
        else {
            printf("%-40s%s\n", "CPU (less than) Threshold:", "n/a");
        }
        if (self->CpuUpperThreshold != -1) {
            printf("%-40s%s%d\n", "CPU (greater than/equal) Threshold:", ">= ", self->CpuUpperThreshold);
        } else {
            printf("%-40s%s\n", "CPU (greater than/equal) Threshold:", "n/a");
        }


        // Memory
        if (self->MemoryThreshold != -1) {
            if (self->bMemoryTriggerBelowValue) {
                printf("%-40s<%d\n", "Commit Threshold:", self->MemoryThreshold);
            } else {
                printf("%-40s>=%d\n", "Commit Threshold:", self->MemoryThreshold);
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
            printf("\n%-40s%s_<counter>.<pid>\n", "Custom name for core dumps:", self->CoreDumpName);
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
    if(self->ProcessGroupId != NO_PID && kill(-1 * self->ProcessGroupId, 0)) {
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
    printf("\nProcDump v1.2 - Sysinternals process dump utility\n");
    printf("Copyright (C) 2020 Microsoft Corporation. All rights reserved. Licensed under the MIT license.\n");
    printf("Mark Russinovich, Mario Hewardt, John Salem, Javid Habibi\n");

    printf("Monitors a process and writes a dump file when the process meets the\n");
    printf("specified criteria.\n\n");
}


//--------------------------------------------------------------------
//
// PrintUsage - Print usage
//
//--------------------------------------------------------------------
int PrintUsage(struct ProcDumpConfiguration *self)
{
    printf("\nUsage: procdump [OPTIONS...] TARGET\n");
    printf("   OPTIONS\n");
    printf("      -h          Prints this help screen\n");
    printf("      -C          Trigger core dump generation when CPU exceeds or equals specified value (0 to 100 * nCPU)\n");
    printf("      -c          Trigger core dump generation when CPU is less than specified value (0 to 100 * nCPU)\n");
    printf("      -M          Trigger core dump generation when memory commit exceeds or equals specified value (MB)\n");
    printf("      -m          Trigger core dump generation when when memory commit is less than specified value (MB)\n");
    printf("      -T          Trigger when thread count exceeds or equals specified value.\n");
    printf("      -F          Trigger when file descriptor count exceeds or equals specified value.\n");  
    printf("      -G          Trigger when signal with the specified value (num) is sent (uses PTRACE and will affect performance of target process).\n");  
    printf("      -I          Polling frequency in milliseconds (default is %d)\n", MIN_POLLING_INTERVAL);        
    printf("      -n          Number of core dumps to write before exiting (default is %d)\n", DEFAULT_NUMBER_OF_DUMPS);
    printf("      -s          Consecutive seconds before dump is written (default is %d)\n", DEFAULT_DELTA_TIME);
    printf("      -o          Path and/or filename prefix where the core dump is written to\n");
    printf("      -d          Writes diagnostic logs to syslog\n");
    printf("   TARGET must be exactly one of these:\n");
    printf("      -p          pid of the process\n");
    printf("      -g          pgid of the process group\n");
    printf("      -w          Name of the process executable\n\n");

    return -1;
}
