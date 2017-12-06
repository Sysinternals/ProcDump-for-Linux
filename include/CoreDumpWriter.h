// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

//--------------------------------------------------------------------
//
// Core dump orchestrator
//
//--------------------------------------------------------------------

#ifndef CORE_DUMP_WRITER_H
#define CORE_DUMP_WRITER_H

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include "Handle.h"
#include "ProcDumpConfiguration.h"

#define DATE_LENGTH 26
#define MAX_LINES 15
#define BUFFER_LENGTH 1024

enum ECoreDumpType {
    COMMIT,
    CPU,
    TIME,
    MANUAL
};

struct CoreDumpWriter {
    struct ProcDumpConfiguration *Config;
    enum ECoreDumpType Type;
};

struct CoreDumpWriter *NewCoreDumpWriter(enum ECoreDumpType type, struct ProcDumpConfiguration *config);

int WriteCoreDump(struct CoreDumpWriter *self);

#endif // CORE_DUMP_WRITER_H