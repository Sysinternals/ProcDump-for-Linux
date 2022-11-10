// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// .NET helpers
//
//--------------------------------------------------------------------

#ifndef DOTNETHELPERS_H
#define DOTNETHELPERS_H

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

bool IsCoreClrProcess(pid_t pid, char** socketName);
bool GenerateCoreClrDump(char* socketName, char* dumpFileName);

#endif // DOTNETHELPERS_H