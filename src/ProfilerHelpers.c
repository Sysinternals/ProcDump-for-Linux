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

extern char* socketPath;

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
int LoadProfiler(pid_t pid, char* filter)
{
    int fd = 0;
    struct sockaddr_un addr = {0};
    void* temp_buffer = NULL;
    uint32_t attachTimeout = 5000;
    struct CLSID profilerGuid = {0};
    uint16_t* profilerPathW = NULL;
    unsigned int clientDataSize = 0;
    char* socketName = NULL;
    char* clientData = NULL;

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
        clientDataSize = snprintf(NULL, 0, "%d;%s", getpid(), filter) + 1;
        clientData = malloc(clientDataSize);
        sprintf(clientData, "%d;%s", getpid(), filter);
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
int InjectProfiler(pid_t pid, char* filter)
{
    int ret = ExtractProfiler();
    if(ret != 0)
    {
        Log(error, "Failed to extract profiler. Please make sure you are running elevated.");
        Trace("InjectProfiler: failed to extract profiler.");
        return ret;
    }

    ret = LoadProfiler(pid, filter);
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
//    <procdump_pid>;<full_dump_path>;Exception[;Exception...]
// Where Exception is:
//    Name_of_Exception:num_dumps
// The exception filter passed in from the command line:
//    <Name_ofException>,...
//
//--------------------------------------------------------------------
char* GetEncodedExceptionFilter(char* exceptionFilterCmdLine, unsigned int numDumps)
{
    unsigned int totalExceptionNameLen = 0;
    unsigned int numberOfDumpsLen = 0;
    unsigned int numExceptions = 0;
    char* exceptionFilterCur = NULL;
    char* exceptionFilter = NULL;
    char tmp[10];

    char* cpy = strdup(exceptionFilterCmdLine);

    numberOfDumpsLen = sprintf(tmp, "%d", numDumps);

    char* token = strtok(cpy, ",");
    while(token!=NULL)
    {
        totalExceptionNameLen += strlen(token);
        numExceptions++;
        token = strtok(NULL, ",");
    }

    free(cpy);
    cpy = strdup(exceptionFilterCmdLine);

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
    char* tmpFolder = NULL;

    tmpFolder = GetSocketPath("procdump/procdump-cancel-", pid);

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

//--------------------------------------------------------------------
//
// WaitForProfilerCompletion
//
// Waits for profiler to send a completed status (bool=true) on
// <prefix>/procdump-status-<pid>
//
//--------------------------------------------------------------------
int WaitForProfilerCompletion(pid_t pid)
{
    unsigned int s, t, s2;
    struct sockaddr_un local, remote;
    int len;
    char* tmpFolder = NULL;

    tmpFolder = GetSocketPath("procdump/procdump-status-", getpid());
    socketPath = tmpFolder;
    Trace("Status socket path: %s", tmpFolder);

    s = socket(AF_UNIX, SOCK_STREAM, 0);    // TODO: Failure

    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, tmpFolder);
    unlink(local.sun_path);
    len = strlen(local.sun_path) + sizeof(local.sun_family);
    bind(s, (struct sockaddr *)&local, len);    // TODO: Failure
    chmod(tmpFolder, 0777);

    listen(s, 1);       // Only allow one client to connect

    while(true)
    {
        bool res=false;
        Trace("CancelThread:Waiting for status");

        t = sizeof(remote);
        s2 = accept(s, (struct sockaddr *)&remote, &t);

        Trace("CancelThread:Connected to status");

        recv(s2, &res, sizeof(bool), 0);
        if(res==true)
        {
            Trace("Profiler reported that it's done");

            close(s2);
            break;
        }

        close(s2);
    }

    unlink(tmpFolder);
    socketPath = NULL;

    if(tmpFolder)
    {
        free(tmpFolder);
    }
    return 0;
}