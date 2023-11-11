// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// Restrack.h
//
//--------------------------------------------------------------------

#ifndef RESTRACK_H
#define RESTRACK_H

#define MAX_CALL_STACK_FRAMES   100

#define MALLOC_ALLOC       0x00000001
#define MALLOC_FREE        0x00000002
#define CALLOC_ALLOC       0x00000003
#define REALLOC_ALLOC      0x00000004
#define REALLOCARRAY_ALLOC 0x00000005

struct ResourceInformation
{
    unsigned long allocAddress;
    uint64_t pid;
    unsigned int resourceType;
    unsigned long allocSize;
    long callStackLen;
    __u64 stackTrace[MAX_CALL_STACK_FRAMES];
};

struct procdump_ebpf* RunRestrack(struct ProcDumpConfiguration *config, pid_t targetPid);
void StopRestrack(struct procdump_ebpf* skel);
int RestrackHandleEvent(void *ctx, void *data, size_t data_sz);
void ReportLeaks(ProcDumpConfiguration* config);

#endif // RESTRACK_H

