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

struct procdump_ebpf* RunRestrack(struct ProcDumpConfiguration *config);
void StopRestrack(struct procdump_ebpf* skel);
int RestrackHandleEvent(void *ctx, void *data, size_t data_sz);
void* ReportLeaks(void* args);
pthread_t WriteRestrackSnapshot(ProcDumpConfiguration* config, ECoreDumpType type);

#endif // RESTRACK_H

