// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// General purpose helpers
//
//--------------------------------------------------------------------

#ifndef GENHELPERS_H
#define GENHELPERS_H

#include <linux/version.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/utsname.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>

#define MIN_KERNEL_VERSION 3
#define MIN_KERNEL_PATCH 5

//-------------------------------------------------------------------------------------
// Auto clean up of memory using free (void)
//-------------------------------------------------------------------------------------
static inline void cleanup_void(void* val)
{
    void **ppVal = (void**)val;
    free(*ppVal);
}

//-------------------------------------------------------------------------------------
// Auto clean up of file descriptors using close
//-------------------------------------------------------------------------------------
static inline void cleanup_fd(int* val)
{
    if (*val)
    {
        close(*val);
    }
}

//-------------------------------------------------------------------------------------
// Auto clean up of dir using closedir
//-------------------------------------------------------------------------------------
static inline void cleanup_dir(DIR** val)
{
    if(*val)
    {
        closedir(*val);
    }
}

//-------------------------------------------------------------------------------------
// Auto clean up of FILE using fclose
//-------------------------------------------------------------------------------------
static inline void cleanup_file(FILE** val)
{
    if(*val)
    {
        fclose(*val);
    }
}

//-------------------------------------------------------------------------------------
// Auto cancel pthread
//-------------------------------------------------------------------------------------
static inline void cancel_pthread(unsigned long* val)
{
    if(*val!=-1)
    {
        pthread_cancel(*val);
    }
}

#define auto_free __attribute__ ((__cleanup__(cleanup_void)))
#define auto_free_fd __attribute__ ((__cleanup__(cleanup_fd)))
#define auto_free_dir __attribute__ ((__cleanup__(cleanup_dir)))
#define auto_free_file __attribute__ ((__cleanup__(cleanup_file)))
#define auto_cancel_thread __attribute__ ((__cleanup__(cancel_pthread)))

bool ConvertToInt(const char* src, int* conv);
bool IsValidNumberArg(const char *arg);
bool CheckKernelVersion();
uint16_t* GetUint16(char* buffer);
char* GetPath(char* lineBuf);
FILE *popen2(const char *command, const char *type, pid_t *pid);
char *sanitize(char *processName);
int StringToGuid(char* szGuid, struct CLSID* pGuid);
int GetHex(char* szStr, int size, void* pResult);
bool createDir(const char *dir, mode_t perms);
char* GetSocketPath(char* prefix, pid_t pid, pid_t targetPid);
int send_all(int socket, void *buffer, size_t length);
int recv_all(int socket, void* buffer, size_t length);

#endif // GENHELPERS_H

