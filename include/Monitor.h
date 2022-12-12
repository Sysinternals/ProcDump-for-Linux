// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// Monitor functions
//
//--------------------------------------------------------------------

#ifndef MONITOR_H
#define MONITOR_H

#include <signal.h>
#include <sys/ptrace.h>
#include <stdlib.h>

#include "ProcDumpConfiguration.h"

#define MAX_PROFILER_CONNECTIONS    50

// Monitor functions
void MonitorProcesses(struct ProcDumpConfiguration*self);
int CreateMonitorThreads(struct ProcDumpConfiguration *self);
int StartMonitor(struct ProcDumpConfiguration* monitorConfig);
int WaitForQuit(struct ProcDumpConfiguration *self, int milliseconds);
int WaitForQuitOrEvent(struct ProcDumpConfiguration *self, struct Handle *handle, int milliseconds);
int WaitForAllMonitorsToTerminate(struct ProcDumpConfiguration *self);
int WaitForSignalThreadToTerminate(struct ProcDumpConfiguration *self);
bool IsQuit(struct ProcDumpConfiguration *self);
int SetQuit(struct ProcDumpConfiguration *self, int quit);
bool ContinueMonitoring(struct ProcDumpConfiguration *self);
bool BeginMonitoring(struct ProcDumpConfiguration *self);

// Monitor worker threads
void *CommitMonitoringThread(void *thread_args /* struct ProcDumpConfiguration* */);
void *CpuMonitoringThread(void *thread_args /* struct ProcDumpConfiguration* */);
void *ThreadCountMonitoringThread(void *thread_args /* struct ProcDumpConfiguration* */);
void *FileDescriptorCountMonitoringThread(void *thread_args /* struct ProcDumpConfiguration* */);
void *SignalMonitoringThread(void *thread_args /* struct ProcDumpConfiguration* */);
void *TimerThread(void *thread_args /* struct ProcDumpConfiguration* */);
void *ExceptionMonitoringThread(void *thread_args /* struct ProcDumpConfiguration* */);
void *ProcessMonitor(void *thread_args /* struct ProcDumpConfiguration* */);
void *WaitForProfilerCompletion(void *thread_args /* struct ProcDumpConfiguration* */);

#endif // MONITOR_H