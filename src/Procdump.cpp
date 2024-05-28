// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// This program monitors a process and generates core dumps in
// in response to various triggers
//
//--------------------------------------------------------------------
#include "Includes.h"

extern struct ProcDumpConfiguration g_config;

//--------------------------------------------------------------------
//
// OnExit
//
// Invoked when ProcDump exits.
//
//--------------------------------------------------------------------
void OnExit()
{
    ExitProcDump();
}


//--------------------------------------------------------------------
//
// main
//
// main ProcDump function
//
//--------------------------------------------------------------------
int main(int argc, char *argv[])
{
    // print banner and begin initialization
    PrintBanner();
    InitProcDump();

    // Parse command line arguments
    if (GetOptions(&g_config, argc, argv) != 0)
    {
        exit(-1);
    }

    // Register exit handler
    atexit(OnExit);

    // monitor for all specified processes
    MonitorProcesses(&g_config);
}
