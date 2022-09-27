// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// This program monitors a process and generates core dumps in
// in response to various triggers
//
//--------------------------------------------------------------------

#include "Procdump.h"
extern struct ProcDumpConfiguration g_config;

int main(int argc, char *argv[])
{
    // print banner and begin intialization
    PrintBanner();
    InitProcDump();

    // print privelege warning
    if(geteuid() != 0)
    {
        Log(warn, "Procdump not running with elevated credentials. If your uid does not match the uid of the target process procdump will not be able to capture memory dumps");
        PrintUsage();
    }
    else
    {
        if (GetOptions(&g_config, argc, argv) != 0)
        {
            Trace("main: failed to parse command line arguments");
            exit(-1);
        }

        // monitor for all specified processes
        MonitorProcesses(&g_config);
    }

    ExitProcDump();
}
