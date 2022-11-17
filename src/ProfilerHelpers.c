// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// General purpose helpers
//
//--------------------------------------------------------------------
#include "Includes.h"

extern char _binary_obj_ProcDumpProfiler_so_end[];
extern char _binary_obj_ProcDumpProfiler_so_start[];

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
    auto_free_fd int destfd = -1;

    // Try to delete the profiler lib in case it was left over...
    unlink(PROCDUMP_DIR "/" PROFILER_FILE_NAME);

    destfd = creat(PROCDUMP_DIR "/" PROFILER_FILE_NAME, S_IRWXU|S_IROTH);
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
            return 1;
        }
        written += writeRet;
    }

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
int LoadProfiler(pid_t pid, char* filter, char* fullDumpPath)
{
    struct sockaddr_un addr = {0};
    uint32_t attachTimeout = 5000;
    struct CLSID profilerGuid = {0};
    unsigned int clientDataSize = 0;

    auto_free_fd int fd = 0;
    auto_free char* socketName = NULL;
    auto_free char* clientData = NULL;
    auto_free char* dumpPath = NULL;
    auto_free uint16_t* profilerPathW = NULL;
    auto_free void* temp_buffer = NULL;

    if(!IsCoreClrProcess(pid, &socketName))
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
        Trace("Failed to connect to socket for .NET Core profiler load.");
        return -1;
    }

    // Calculate header and payload size

    // profile attach timeout
    unsigned int payloadSize = sizeof(attachTimeout);

    // profiler guid
    StringToGuid(PROFILER_GUID, &profilerGuid);
    payloadSize += sizeof(profilerGuid);

    //profiler path
    profilerPathW = GetUint16(PROCDUMP_DIR "/" PROFILER_FILE_NAME);
    unsigned int profilerPathLen = (strlen(PROCDUMP_DIR "/" PROFILER_FILE_NAME)+1);

    //profilerPathW = GetUint16("/home/marioh/profiler/profiler/procdumpprofiler.so");
    //unsigned int profilerPathLen = (strlen("/home/marioh/profiler/profiler/procdumpprofiler.so")+1);

    payloadSize += sizeof(profilerPathLen);
    payloadSize += profilerPathLen*sizeof(uint16_t);

    // client data
    if(filter)
    {
        // client data in the following format: <full_path_to_dump>;<procdump_pid>;Exception1>:<dumpsToCollect>;...
        clientDataSize = snprintf(NULL, 0, "%s;%d;%s", fullDumpPath, getpid(), filter) + 1;
        clientData = malloc(clientDataSize);
        sprintf(clientData, "%s;%d;%s", fullDumpPath, getpid(), filter);
    }
    else
    {
        clientDataSize = 0;
    }
    payloadSize += sizeof(clientDataSize);
    payloadSize += clientDataSize*sizeof(unsigned char);

    Trace("LoadProfiler: Exception list: %s", clientData);

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
        memcpy(temp_buffer_cur, clientData, clientDataSize);
        temp_buffer_cur += clientDataSize;
    }

    // Send the payload
    if(send(fd, temp_buffer, totalPacketSize, 0)==-1)
    {
        Trace("Failed sending packet to diagnostics server [%d]", errno);
        return -1;
    }

    // Get response
    struct IpcHeader retHeader;
    if(recv(fd, &retHeader, sizeof(struct IpcHeader), 0)==-1)
    {
        Trace("Failed receiving response header from diagnostics server [%d]", errno);
        return -1;
    }

    // Check the header to make sure its the right size
    if(retHeader.Size != CORECLR_DIAG_IPCHEADER_SIZE)
    {
        Trace("Failed validating header size in response header from diagnostics server [%d != 24]", retHeader.Size);
        return -1;
    }

    int32_t res = -1;
    if(recv(fd, &res, sizeof(int32_t), 0)==-1)
    {
        Trace("Failed receiving result code from response payload from diagnostics server [%d]", errno);
        return -1;
    }
    else
    {
        if(res!=0)
        {
            if(res==0x8013136A)
            {
                // The profiler is already loaded in the target process
                Trace("Target process is already beeing monitored.");
                Log(error, "Target process is already beeing monitored.");
            }
            else
            {
                Trace("Error returned from diagnostics server [0x%x]", res);
                Log(error, "Error returned from diagnostics server [0x%x]", res);
            }

            return -1;
        }
    }

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
int InjectProfiler(pid_t pid, char* filter, char* fullDumpPath)
{
    int ret = ExtractProfiler();
    if(ret != 0)
    {
        Log(error, "Failed to extract profiler. Please make sure you are running elevated.");
        Trace("InjectProfiler: failed to extract profiler.");
        return ret;
    }

    ret = LoadProfiler(pid, filter, fullDumpPath);
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
// GetEncodedExceptionFilter
//
// Create the exception filter string which goes like this:
//    <fullpathtodumplocation>;<pidofprocdump>;<exception>:<numdumps>;<exception>:<numdumps>,...
// Where Exception is:
//    Name_of_Exception:num_dumps or
//    <any> which means that the user did not specify an exception filter and we just dump on
//          any exception that happens to throw.
// The exception filter passed in from the command line using -f:
//    <Name_ofException>,<Name_ofException>,...
//
//--------------------------------------------------------------------
char* GetEncodedExceptionFilter(char* exceptionFilterCmdLine, unsigned int numDumps)
{
    unsigned int totalExceptionNameLen = 0;
    unsigned int numberOfDumpsLen = 0;
    unsigned int numExceptions = 0;
    char* exceptionFilter = NULL;
    char* exceptionFilterCur = NULL;
    char tmp[10];

    char* cpy = exceptionFilterCmdLine ? strdup(exceptionFilterCmdLine) : strdup("<any>");

    numberOfDumpsLen = sprintf(tmp, "%d", numDumps);

    char* token = strtok(cpy, ",");
    while(token!=NULL)
    {
        totalExceptionNameLen += strlen(token);
        numExceptions++;
        token = strtok(NULL, ",");
    }

    free(cpy);

    cpy = exceptionFilterCmdLine ? strdup(exceptionFilterCmdLine) : strdup("<any>");

    totalExceptionNameLen++; // NULL terminator

    exceptionFilter = malloc(totalExceptionNameLen+numExceptions*(numberOfDumpsLen+2)); // +1 for : seperator +1 for ; seperator
    exceptionFilterCur = exceptionFilter;

    token = strtok(cpy, ",");
    while(token!=NULL)
    {
        exceptionFilterCur += sprintf(exceptionFilterCur, "%s:%d;", token, numDumps);
        token = strtok(NULL, ",");
    }

    free(cpy);
    return exceptionFilter;
}

//--------------------------------------------------------------------
//
// CancelProfiler
//
// Sends a message over socket (procdump-cancel-<target_pid>) to
// indicate that the user has cancelled the procdump session and the
// profiler has to be unloaded.
//
//--------------------------------------------------------------------
int CancelProfiler(pid_t pid)
{
    int s, len;
    struct sockaddr_un remote;
    auto_free char* tmpFolder = NULL;

    tmpFolder = GetSocketPath("procdump/procdump-cancel-", pid, 0);

    if((s = socket(AF_UNIX, SOCK_STREAM, 0))==-1)        // TODO: Errors
    {
        Trace("Failed to create socket\n");
        return -1;
    }

    Trace("Trying to connect...\n");

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, tmpFolder);
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    if(connect(s, (struct sockaddr *)&remote, len)==-1)
    {
        Trace("Failed to connect\n");
        return -1;
    }

    Trace("Connected...\n");

    bool cancel=true;
    if(send(s, &cancel, 1, 0)==-1)
    {
        Trace("Failed to send cancellation\n");
        return -1;
    }

    return 0;
}

//-------------------------------------------------------------------------------------
//
// WaitForProfilerCompletion
//
// Waits for profiler to send a status (bool=true) on (<prefix>/procdump-status-<pid>)
// that indicates that a dump was generated. We wait here until the dump condition
// (number of dumps) has been satisfied until we return. For example, if the user
// specified they wanted 3 dumps on System.InvalidOperationException, we will get three
// status (bool==true) before we return.
//
// The socket created is in the form: <socket_path>/procdump-status-<procdumpPid>-<targetPid>
//
//-------------------------------------------------------------------------------------
int WaitForProfilerCompletion(struct ProcDumpConfiguration* config)
{
    unsigned int t, s2;
    struct sockaddr_un local, remote;
    int len;
    pthread_t processMonitor;

    auto_free char* tmpFolder = NULL;
    auto_free_fd int s=-1;

    tmpFolder = GetSocketPath("procdump/procdump-status-", getpid(), config->ProcessId);
    config->socketPath = tmpFolder;
    Trace("Status socket path: %s", tmpFolder);

    s = socket(AF_UNIX, SOCK_STREAM, 0);    // TODO: Failure

    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, tmpFolder);
    unlink(local.sun_path);
    len = strlen(local.sun_path) + sizeof(local.sun_family);
    bind(s, (struct sockaddr *)&local, len);    // TODO: Failure
    chmod(tmpFolder, 0777);

    //
    // Create a thread that will monitor for abnormal process terminations of the target process.
    // In case of an abnormal process termination, it cancels the socket that procdump is waiting on
    // for status from target process and we can exit cleanly.
    //
    config->statusSocket = s;
    if ((pthread_create(&processMonitor, NULL, ProcessMonitor, (void *) config)) != 0)
    {
        Trace("WaitForProfilerCompletion: failed to create ProcessMonitor.");
        return -1;
    }


    listen(s, 1);       // Only allow one client to connect

    int dumpsGenerated = 0;

    while(true)
    {
        Trace("CancelThread:Waiting for status");

        t = sizeof(remote);
        s2 = accept(s, (struct sockaddr *)&remote, &t);
        if(s2==-1)
        {
            // This means the target process died and we need to return
            return -1;
        }

        Trace("CancelThread:Connected to status");

        // packet looks like this: <payload_len><[byte] 0=failure, 1=success><[uint_32] dumpfile_path_len><[char*]Dumpfile path>
        int payloadLen = 0;
        recv(s2, &payloadLen, sizeof(int), 0);
        if(payloadLen>0)
        {
            Trace("Received payload len %d", payloadLen);
            char* payload = (char*) malloc(payloadLen);
            recv(s2, payload, payloadLen, 0);

            char status = payload[0];
            Trace("Received status %c", status);

            int dumpLen = 0;
            memcpy(&dumpLen, payload+1, sizeof(int));
            Trace("Received dump length %d", dumpLen);

            char* dump = malloc(dumpLen+1);
            memcpy(dump, payload+1+sizeof(int), dumpLen);
            dump[dumpLen] = '\0';
            Trace("Received dump path %s", dump);

            free(payload);

            if(status=='1')
            {
                Log(info, "Core dump generated: %s", dump);
                dumpsGenerated++;
                if(dumpsGenerated == config->NumberOfDumpsToCollect)
                {
                    free(dump);
                    Trace("Total dump count has been reached: %d", dumpsGenerated);
                    break;
                }
            }
            else if(status=='2')
            {
                Log(error, "Failed to generate core dump: %s", dump);
            }
            else if(status=='F')
            {
                Log(error, "Exception monitoring failed.");
                Trace("Total dump count has been reached: %d", dumpsGenerated);
                free(dump);
                break;
            }

            free(dump);
        }
    }

    unlink(tmpFolder);
    config->socketPath = NULL;

    return 0;
}