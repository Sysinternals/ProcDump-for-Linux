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

extern char _binary_obj_ProcDumpProfiler_so_end[];
extern char _binary_obj_ProcDumpProfiler_so_start[];

extern pthread_mutex_t queue_mutex;

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
    self->bDumpOnException =            false;
    self->bDumpOnException =            NULL;
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

    if(self->WaitingForProcessName)
    {
        free(self->ProcessName);
    }

    /* TODO: Crash
    if(self->ExceptionFilter)
    {
        free(self->ExceptionFilter);
    }

    free(self->CoreDumpPath);
    free(self->CoreDumpName);
    */
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
// GetHex
//
// Gets hex value of specified string
//
//--------------------------------------------------------------------
int GetHex(char* szStr, int size, void* pResult)
{
    int         count = size * 2;       // # of bytes to take from string.
    unsigned int Result = 0;           // Result value.
    char          ch;

    while (count-- && (ch = *szStr++) != '\0')
    {
        switch (ch)
        {
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
            Result = 16 * Result + (ch - '0');
            break;

            case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
            Result = 16 * Result + 10 + (ch - 'A');
            break;

            case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
            Result = 16 * Result + 10 + (ch - 'a');
            break;

            default:
            return -1;
        }
    }

    // Set the output.
    switch (size)
    {
        case 1:
        *((unsigned char*) pResult) = (unsigned char) Result;
        break;

        case 2:
        *((short*) pResult) = (short) Result;
        break;

        case 4:
        *((int*) pResult) = Result;
        break;

        default:
        break;
    }

    return 0;
}

//--------------------------------------------------------------------
//
// StringToGuid
//
// Convert string representation of GUID to a GUID
//
//--------------------------------------------------------------------
int StringToGuid(char* szGuid, struct CLSID* pGuid)
{
    int i;

    // Verify the surrounding syntax.
    if (strlen(szGuid) != 38 || szGuid[0] != '{' || szGuid[9] != '-' ||
        szGuid[14] != '-' || szGuid[19] != '-' || szGuid[24] != '-' || szGuid[37] != '}')
    {
        return -1;
    }

    // Parse the first 3 fields.
    if (GetHex(szGuid + 1, 4, &pGuid->Data1))
        return -1;
    if (GetHex(szGuid + 10, 2, &pGuid->Data2))
        return -1;
    if (GetHex(szGuid + 15, 2, &pGuid->Data3))
        return -1;

    // Get the last two fields (which are byte arrays).
    for (i = 0; i < 2; ++i)
    {
        if (GetHex(szGuid + 20 + (i * 2), 1, &pGuid->Data4[i]))
        {
            return -1;
        }
    }
    for (i=0; i < 6; ++i)
    {
        if (GetHex(szGuid + 25 + (i * 2), 1, &pGuid->Data4[i+2]))
        {
            return -1;
        }
    }
    return 0;
}

//--------------------------------------------------------------------
//
// createDir
//
// Create specified directory with specified permissions.
//
//--------------------------------------------------------------------
bool createDir(const char *dir, mode_t perms)
{
    if (dir == NULL) {
        fprintf(stderr, "createDir invalid params\n");
        return false;
    }

    struct stat st;

    if (stat(dir, &st) < 0) {
        if (mkdir(dir, perms) < 0) {
            return false;
        }
    } else {
        if (!S_ISDIR(st.st_mode)) {
            return false;
        }
        chmod(dir, perms);
    }
    return true;
}

//--------------------------------------------------------------------
//
// ExtractProfiler
//
// The profiler so is embedded into the ProcDump binary. This function
// extracts the profiler so and places it into /opt/procdump
//
//--------------------------------------------------------------------
int ExtractProfiler()
{
    // Try to delete the profiler lib in case it was left over...
    unlink(PROCDUMP_DIR "/" PROFILER_FILE_NAME);

    int destfd = creat(PROCDUMP_DIR "/" PROFILER_FILE_NAME, S_IRWXU|S_IROTH);
    if (destfd < 0)
    {
        return -1;
    }

    size_t written = 0;
    ssize_t writeRet;
    size_t size = _binary_obj_ProcDumpProfiler_so_end - _binary_obj_ProcDumpProfiler_so_start;

    while (written < size)
    {
        writeRet = write(destfd, _binary_obj_ProcDumpProfiler_so_start + written, size - written);
        if (writeRet < 0)
        {
            close(destfd);
            return 1;
        }
        written += writeRet;
    }

    close(destfd);

    return 0;
}

//--------------------------------------------------------------------
//
// LoadProfiler
//
// This function sends a command to the diagnostics pipe of the target
// process instructing the runtime to load the profiler.
//
//--------------------------------------------------------------------
int LoadProfiler(struct ProcDumpConfiguration* monitorConfig)
{
    int fd = 0;
    struct sockaddr_un addr = {0};
    void* temp_buffer = NULL;
    uint32_t attachTimeout = 5000;
    struct CLSID profilerGuid = {0};
    uint16_t* profilerPathW = NULL;
    unsigned int clientDataSize = 0;
    char* socketName = NULL;

    if(!IsCoreClrProcess(monitorConfig->ProcessId, &socketName))
    {
        Trace("LoadProfiler: Unable to find .NET diagnostics endpoint for targeted process.");
        return -1;
    }

    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
    {
        Trace("Failed to create socket for .NET Core dump generation.");
        return -1;
    }

    // Create socket to diagnostics server
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketName, sizeof(addr.sun_path)-1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(struct sockaddr_un)) == -1)
    {
        Trace("Failed to connect to socket for .NET Core dump generation.");
        return -1;
    }

    // Calculate header and payload size

    // profile attach timeout
    unsigned int payloadSize = sizeof(attachTimeout);

    // profiler guid
    StringToGuid(PROFILER_GUID, &profilerGuid);
    payloadSize += sizeof(profilerGuid);

    // profiler path
    //profilerPathW = GetUint16(PROCDUMP_DIR "/" PROFILER_FILE_NAME);
    //printf(PROCDUMP_DIR "/" PROFILER_FILE_NAME);
    //printf("\n");
    //unsigned int profilerPathLen = (strlen(PROCDUMP_DIR "/" PROFILER_FILE_NAME)+1);

    profilerPathW = GetUint16("/home/marioh/profiler/profiler/procdumpprofiler.so");
    unsigned int profilerPathLen = (strlen("/home/marioh/profiler/profiler/procdumpprofiler.so")+1);

    payloadSize += sizeof(profilerPathLen);
    payloadSize += profilerPathLen*sizeof(uint16_t);

    // client data
    if(monitorConfig->ExceptionFilter)
    {
        clientDataSize = strlen(monitorConfig->ExceptionFilter);
    }
    else
    {
        clientDataSize = 0;
    }
    payloadSize += sizeof(clientDataSize);
    payloadSize += clientDataSize*sizeof(char);

    uint16_t totalPacketSize = sizeof(struct IpcHeader)+payloadSize;

    // First initialize header
    temp_buffer = malloc(totalPacketSize);
    if(temp_buffer==NULL)
    {
        Trace("Failed allocating memory");
        return -1;
    }

    memset(temp_buffer, 0, totalPacketSize);
    struct IpcHeader dumpHeader =
    {
        { {"DOTNET_IPC_V1"} },
        (uint16_t)totalPacketSize,
        (uint8_t)0x03,
        (uint8_t)0x01,
        (uint16_t)0x0000
    };

    void* temp_buffer_cur = temp_buffer;

    memcpy(temp_buffer_cur, &dumpHeader, sizeof(struct IpcHeader));
    temp_buffer_cur += sizeof(struct IpcHeader);

    memcpy(temp_buffer_cur, &attachTimeout, sizeof(attachTimeout));
    temp_buffer_cur += sizeof(attachTimeout);

    memcpy(temp_buffer_cur, &profilerGuid, sizeof(profilerGuid));
    temp_buffer_cur += sizeof(profilerGuid);

    memcpy(temp_buffer_cur, &profilerPathLen, sizeof(profilerPathLen));
    temp_buffer_cur += sizeof(profilerPathLen);

    memcpy(temp_buffer_cur, profilerPathW, profilerPathLen*sizeof(uint16_t));
    temp_buffer_cur += profilerPathLen*sizeof(uint16_t);

    memcpy(temp_buffer_cur, &clientDataSize, sizeof(unsigned int));
    temp_buffer_cur += sizeof(unsigned int);

    if(clientDataSize>0)
    {
        memcpy(temp_buffer_cur, monitorConfig->ExceptionFilter, clientDataSize);
        temp_buffer_cur += clientDataSize;
    }

    // Send the payload
    if(send(fd, temp_buffer, totalPacketSize, 0)==-1)
    {
        Trace("Failed sending packet to diagnostics server [%d]", errno);
        free(profilerPathW);
        free(temp_buffer);
        return -1;
    }

    // Get response
    struct IpcHeader retHeader;
    if(recv(fd, &retHeader, sizeof(struct IpcHeader), 0)==-1)
    {
        Trace("Failed receiving response header from diagnostics server [%d]", errno);
        free(profilerPathW);
        free(temp_buffer);
        return -1;
    }

    // Check the header to make sure its the right size
    if(retHeader.Size != CORECLR_DIAG_IPCHEADER_SIZE)
    {
        Trace("Failed validating header size in response header from diagnostics server [%d != 24]", retHeader.Size);
        free(profilerPathW);
        free(temp_buffer);
        return -1;
    }

    int32_t res = -1;
    if(recv(fd, &res, sizeof(int32_t), 0)==-1)
    {
        Trace("Failed receiving result code from response payload from diagnostics server [%d]", errno);
        free(profilerPathW);
        free(temp_buffer);
        return -1;
    }
    else
    {
        if(res!=0)
        {
            Trace("Error returned from diagnostics server [%d]", res);
            Log(error, "Error returned from diagnostics server [%d]", res);
            free(profilerPathW);
            free(temp_buffer);
            return -1;
        }
    }

    free(profilerPathW);
    free(temp_buffer);
    return 0;
}

//--------------------------------------------------------------------
//
// InjectProfiler
//
// Injects the profiler into the target process by:
//
// 1. Extracting the profiler binary from the procdump binary and placing in
//    /opt/procdump. This location is by default protected by sudo.
// 2. Send a command to the target process diagnostics pipe with the location
//    of the profiler (passing in any relevant data such as exception filter and
//    dump count). The profiler opens an IPC pipe that ProcDump will communicate with
//    to get status
//
//--------------------------------------------------------------------
int InjectProfiler(struct ProcDumpConfiguration* monitorConfig)
{
    int ret = ExtractProfiler();
    if(ret != 0)
    {
        Log(error, "Failed to extract profiler. Please make sure you are running elevated.");
        Trace("InjectProfiler: failed to extract profiler.");
        return ret;
    }

    ret = LoadProfiler(monitorConfig);
    if(ret != 0)
    {
        Log(error, "Failed to load profiler. Please make sure you are running elevated and targetting a .NET process.");
        Trace("InjectProfiler: failed to load profiler into target process.");
        return ret;
    }

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
    printf("            [-e]\n");
    printf("            [-f <Exception_Name>]\n");
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
    printf("   -e      [.NET] Create dump when the process encounters an exception\n");
    printf("   -f      [.NET] Filter on the name of the exception.\n");
    printf("   -pf     Polling frequency.\n");
    printf("   -o      Overwrite existing dump file.\n");
    printf("   -log    Writes extended ProcDump tracing to syslog.\n");
    printf("   -w      Wait for the specified process to launch if it's not running.\n");
    printf("   -pgid   Process ID specified refers to a process group ID.\n");

    return -1;
}