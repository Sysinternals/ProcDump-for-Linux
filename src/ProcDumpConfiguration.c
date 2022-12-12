// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// The global configuration structure and utilities header
//
//--------------------------------------------------------------------
#include "Includes.h"

extern pthread_mutex_t LoggerLock;
long HZ;                                                        // clock ticks per second
int MAXIMUM_CPU;                                                // maximum cpu usage percentage (# cores * 100)
struct ProcDumpConfiguration g_config;                          // backbone of the program
struct ProcDumpConfiguration * target_config;                   // list of configs for target group processes or matching names
extern pthread_mutex_t queue_mutex;

sigset_t sig_set;

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

    sigemptyset (&sig_set);
    sigaddset (&sig_set, SIGINT);
    sigaddset (&sig_set, SIGTERM);
    pthread_sigmask (SIG_BLOCK, &sig_set, NULL);

    auto_free char* prefixTmpFolder = NULL;

    // Create the directories where our sockets will be stored
    // If $TMPDIR is set, use it as the path, otherwise we use /tmp
    prefixTmpFolder = getenv("TMPDIR");
    if(prefixTmpFolder==NULL)
    {
        createDir("/tmp/procdump", 0777);
    }
    else
    {
        int len = strlen(prefixTmpFolder) + strlen("/procdump") + 1;
        char* t = malloc(len);
        sprintf(t, "%s%s", prefixTmpFolder, "/procdump");
        createDir(t, 0777);
        free(t);
    }
}

//--------------------------------------------------------------------
//
// ExitProcDump - cleanup during exit.
//
//--------------------------------------------------------------------
void ExitProcDump()
{
    Trace("ExitProcDump: Enter");
    pthread_mutex_destroy(&LoggerLock);
    closelog();

    // Try to delete the profiler lib in case it was left over...
    unlink(PROCDUMP_DIR "/" PROFILER_FILE_NAME);
    Trace("ExitProcDump: Exit");
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
    self->bDumpOnException =            false;
    self->bDumpOnException =            NULL;

    self->socketPath =                  NULL;
    self->statusSocket =                -1;
}

//--------------------------------------------------------------------
//
// FreeProcDumpConfiguration - ensure destruction of config and contents
//
//--------------------------------------------------------------------
void FreeProcDumpConfiguration(struct ProcDumpConfiguration *self)
{
    Trace("FreeProcDumpConfiguration: Enter");
    DestroyEvent(&(self->evtCtrlHandlerCleanupComplete.event));
    DestroyEvent(&(self->evtBannerPrinted.event));
    DestroyEvent(&(self->evtConfigurationPrinted.event));
    DestroyEvent(&(self->evtDebugThreadInitialized.event));
    DestroyEvent(&(self->evtQuit.event));
    DestroyEvent(&(self->evtStartMonitoring.event));

    pthread_mutex_destroy(&self->ptrace_mutex);
    sem_destroy(&(self->semAvailableDumpSlots.semaphore));

    if(self->ProcessName)
    {
        free(self->ProcessName);
        self->ProcessName = NULL;
    }

    if(self->statusSocket != -1)
    {
        close(self->statusSocket);
        self->statusSocket = -1;
    }

    if(self->socketPath)
    {
        unlink(self->socketPath);
        free(self->socketPath);
        self->socketPath = NULL;
    }

    if(self->ExceptionFilter)
    {
        free(self->ExceptionFilter);
        self->ExceptionFilter = NULL;
    }

    if(self->CoreDumpPath)
    {
        free(self->CoreDumpPath);
        self->CoreDumpPath = NULL;
    }

    if(self->CoreDumpName)
    {
        free(self->CoreDumpName);
        self->CoreDumpName = NULL;
    }

    Trace("FreeProcDumpConfiguration: Exit");
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
        copy->ExceptionFilter = self->ExceptionFilter == NULL ? NULL : strdup(self->ExceptionFilter);
        copy->socketPath = self->socketPath == NULL ? NULL : strdup(self->socketPath);
        copy->bDumpOnException = self->bDumpOnException;
        copy->statusSocket = self->statusSocket;

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

            if(self->NumberOfDumpsToCollect > MAX_DUMP_COUNT)
            {
                Log(error, "Max dump count must be less than %d.", MAX_DUMP_COUNT);
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
        else if( 0 == strcasecmp( argv[i], "/e" ) ||
                    0 == strcasecmp( argv[i], "-e" ))
        {
            self->bDumpOnException = true;
        }
        else if( 0 == strcasecmp( argv[i], "/f" ) ||
                   0 == strcasecmp( argv[i], "-f" ))
        {
            if( i+1 >= argc ) return PrintUsage();

            self->ExceptionFilter = strdup(argv[i+1]);
            i++;
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

    // If exception filter is provided with no -e switch exit
    if((self->ExceptionFilter && self->bDumpOnException == false))
    {
        Log(error, "Please use the -e switch when specifying an exception filter (-f)");
        return PrintUsage();
    }

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
    if(self->SignalNumber != -1 || self->bDumpOnException)
    {
        if(self->CpuThreshold != -1 || self->ThreadThreshold != -1 || self->FileDescriptorThreshold != -1 || self->MemoryThreshold != -1)
        {
            Log(error, "Signal/Exception trigger must be the only trigger specified.");
            return PrintUsage();
        }
        if(self->PollingInterval != -1)
        {
            Log(error, "Polling interval has no meaning during Signal/Exception monitoring.");
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
        // Signal
        if (self->bDumpOnException) {
            printf("%-40s%s\n", "Exception monitor", "On");
            printf("%-40s%s\n", "Exception filter", self->ExceptionFilter);
        }
        else {
            printf("%-40s%s\n", "Exception monitor", "Off");
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
            printf("%-40s%s_<counter>\n", "Custom name for core dumps:", self->CoreDumpName);
        }

        SetEvent(&self->evtConfigurationPrinted.event);
        return true;
    }
    return false;
}

//--------------------------------------------------------------------
//
// PrintBanner - Not re-entrant safe banner printer. Function must be called before trigger threads start.
//
//--------------------------------------------------------------------
void PrintBanner()
{
    printf("\nProcDump v1.4 - Sysinternals process dump utility\n");
    printf("Copyright (C) 2022 Microsoft Corporation. All rights reserved. Licensed under the MIT license.\n");
    printf("Mark Russinovich, Mario Hewardt, John Salem, Javid Habibi\n");
    printf("Sysinternals - www.sysinternals.com\n\n");

    printf("Monitors one or more processes and writes a core dump file when the processes exceeds the\n");
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
    printf("            [-e]\n");
    printf("            [-f Include_Filter,...]\n");
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
    printf("   -e      [.NET] Create dump when the process encounters an exception.\n");
    printf("   -f      [.NET] Filter (include) on the (comma seperated) exception name(s).\n");
    printf("   -pf     Polling frequency.\n");
    printf("   -o      Overwrite existing dump file.\n");
    printf("   -log    Writes extended ProcDump tracing to syslog.\n");
    printf("   -w      Wait for the specified process to launch if it's not running.\n");
    printf("   -pgid   Process ID specified refers to a process group ID.\n");

    return -1;
}
