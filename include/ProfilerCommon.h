// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// Profiler Common
//
//--------------------------------------------------------------------
#ifndef PROFILERCOMMON_H
#define PROFILERCOMMON_H

enum TriggerType
{
    Processor,
    Commit,
    Timer,
    Signal,
    ThreadCount,
    FileDescriptorCount,
    Exception
};

#endif // PROFILERCOMMON_H