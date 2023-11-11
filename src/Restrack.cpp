// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// Restrack.cpp
//
//--------------------------------------------------------------------
#include "Includes.h"

#include <sys/time.h>
#include <sys/resource.h>

// These are the start and end addresses to the embedded profiler binary
extern char _binary_procdump_ebpf_o_end[];
extern char _binary_procdump_ebpf_o_start[];

//--------------------------------------------------------------------
//
// ExtractRestrack
//
// The procdump eBPF program is embedded into the ProcDump binary. This function
// extracts the eBPF program and places it into
// PROCDUMP_DIR "/" RESTRACK_FILE_NAME
//
//--------------------------------------------------------------------
extern "C" {
int ExtractRestrack()
{
    auto_free_fd int destfd = -1;

    // Try to delete the restrack program in case it was left over...
    unlink(PROCDUMP_DIR "/" RESTRACK_FILE_NAME);

    destfd = creat(PROCDUMP_DIR "/" RESTRACK_FILE_NAME, S_IRWXU|S_IROTH);
    if (destfd < 0)
    {
        return -1;
    }

    size_t written = 0;
    ssize_t writeRet;
    size_t size = _binary_procdump_ebpf_o_end - _binary_procdump_ebpf_o_start;

    while (written < size)
    {
        writeRet = write(destfd, _binary_procdump_ebpf_o_start + written, size - written);
        if (writeRet < 0)
        {
            return 1;
        }
        written += writeRet;
    }

    return 0;
}

// ------------------------------------------------------------------------------------------
// libbpf_print_fn
//
// Call back to print eBPF errors
// ------------------------------------------------------------------------------------------
static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
    // TODO: Should go into trace file and error out
	return vfprintf(stderr, format, args);
}

//--------------------------------------------------------------------
//
// RunRestrack
//
// Loads the restrack eBPF program and attaches to the memory alloc
// APIs.
//
//--------------------------------------------------------------------
struct procdump_ebpf* RunRestrack(struct ProcDumpConfiguration *config, pid_t targetPid)
{
    int ret = -1;
    struct procdump_ebpf *skel = NULL;

    //
    // Setup extended error logging
    //
    libbpf_set_print(libbpf_print_fn);

    //
    // Open the eBPF program
    //
    skel = procdump_ebpf__open();
    if (!skel)
    {
        return skel;
    }

    skel->bss->target_PID = targetPid;

    ret = procdump_ebpf__load(skel);
    if (ret)
    {
        return NULL;
    }

    ret = procdump_ebpf__attach(skel);
    if (ret)
    {
        procdump_ebpf__destroy(skel);
        return NULL;
    }

    return skel;
}



// ------------------------------------------------------------------------------------------
// SetMaxRLimit
//
// Sets the rlimit to max. Required by eBPF.
// ------------------------------------------------------------------------------------------
void SetMaxRLimit()
{
    struct rlimit lim = {
        .rlim_cur = RLIM_INFINITY,
        .rlim_max = RLIM_INFINITY,
    };

    setrlimit(RLIMIT_MEMLOCK, &lim);
}


}   // extern "C"
