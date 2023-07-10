// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// Profiler helpers
//
//--------------------------------------------------------------------

#ifndef PROFILERHELPERS_H
#define PROFILERHELPERS_H

#define PROCDUMP_DIR        "/usr/local/bin"
#define PROFILER_FILE_NAME  "procdumpprofiler.so"
#define PROFILER_GUID       "{cf0d821e-299b-5307-a3d8-b283c03916dd}"

int InjectProfiler(pid_t pid, char* clientData);
int LoadProfiler(pid_t pid, char* clientData);
int ExtractProfiler();
char* GetEncodedExceptionFilter(char* exceptionFilterCmdLine, unsigned int numDumps);

#endif // PROFILERHELPERS_H

