// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// A simple logging library for log generation and debugging
//
//--------------------------------------------------------------------

#ifndef LOGGING_H
#define LOGGING_H

#include <syslog.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#define INTERNAL_ERROR "Internal Error has occurred. If problem continues to occur run procudmp with -d flag to trace issue"

// double-macro-stringify to expand __FILE__ and __LINE__ properly when they are injected in files
#define S1(x) #x
#define S2(x) S1(x)
#define LOCATION "in "__FILE__ ", at line " S2(__LINE__)

extern struct ProcDumpConfiguration g_config;


enum LogLevel{
    debug,
    info,   // standard output
    warn,
    crit,
    error
};

void Log(enum LogLevel logLevel, const char *message, ...);

pthread_mutex_t LoggerLock;

void DiagTrace(const char* message, ...);

/*
* Behavior: Trace() prints variable number of information appended with line number of its invocation. 
* Params: 1. [Conditionally Required] format. format is required if non-string-literal were passed in as the 2nd param.
*         2. [Required] variable number of parameters.
* Example:  Trace("%s", strerror(errno)).
*/
#define Trace(format, ...) \
    DiagTrace(format " %s", ##__VA_ARGS__, LOCATION);

#endif // LOGGING_H