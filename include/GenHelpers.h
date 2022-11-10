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

#define MIN_KERNEL_VERSION 3
#define MIN_KERNEL_PATCH 5

bool ConvertToInt(const char* src, int* conv);
bool IsValidNumberArg(const char *arg);
bool CheckKernelVersion();
uint16_t* GetUint16(char* buffer);
char* GetPath(char* lineBuf);
FILE *popen2(const char *command, const char *type, pid_t *pid);
char *sanitize(char *processName);

#endif // GENHELPERS_H

