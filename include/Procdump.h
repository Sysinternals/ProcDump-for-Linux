// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// This program monitors a process and generates core dumps in
// in response to various triggers
//
//--------------------------------------------------------------------

#ifndef UBUNTU_SYSINTERNALS_PROCDUMP_H
#define UBUNTU_SYSINTERNALS_PROCDUMP_H

#include <stdio.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <memory.h>
#include <signal.h>
#include <zconf.h>

#include "ProcDumpConfiguration.h"
#include "Logging.h"

#define MIN_CPU 0                           // minimum CPU value
#define DEFAULT_NUMBER_OF_DUMPS 1           // default number of core dumps taken
#define DEFAULT_DELTA_TIME 10               // default delta time in seconds between core dumps

void termination_handler(int sig_num);

#endif //UBUNTU_SYSINTERNALS_PROCDUMP_H
