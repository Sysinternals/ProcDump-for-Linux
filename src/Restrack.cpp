// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// Restrack.cpp
//
//--------------------------------------------------------------------
#define _Bool bool
#include "procdump_ebpf.skel.h"

#include <sys/time.h>
#include <sys/resource.h>

#include "Includes.h"

#include "bcc_elf.h"
#include "bcc_perf_map.h"
#include "bcc_proc.h"
#include "bcc_syms.h"

#include <vector>
#include <algorithm>
#include <string>

typedef struct {
    unsigned int type;
    unsigned long allocCount;
    unsigned long allocSize;
    unsigned long totalAllocSize;
    unsigned int callStackLen;
    __u64 stackTrace[MAX_CALL_STACK_FRAMES];
} groupedAllocEntry;

typedef struct {
    std::string symbolName;
    std::string demangledSymbolName;
    char* fullName;
    uint64_t offset;
    __u64 pc;
} stackFrame;

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

//--------------------------------------------------------------------
//
// StopRestrack
//
// Stops the Restrack eBPF program
//
//--------------------------------------------------------------------
void StopRestrack(struct procdump_ebpf* skel)
{
    procdump_ebpf__destroy(skel);
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

    SetMaxRLimit();

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
// RestrackHandleEvent
//
// Handles events from the Restrack eBPF program
// ------------------------------------------------------------------------------------------
int RestrackHandleEvent(void *ctx, void *data, size_t data_sz)
{
    Trace("RestrackHandleEvent: Enter");
    ResourceInformation* event = (ResourceInformation*) data;
    struct ProcDumpConfiguration* config = (struct ProcDumpConfiguration*) ctx;

    if(event->allocSize > 0)
    {
        if(event->resourceType == MALLOC_ALLOC || event->resourceType == CALLOC_ALLOC || event->resourceType == REALLOC_ALLOC || event->resourceType == REALLOCARRAY_ALLOC)
        {
            //
            // Add to allocation map
            //
            config->memAllocMap[(uintptr_t) event->allocAddress] = event;
            Trace("Got event: Alloc size: %ld 0x%lx\n", event->allocSize, event->allocAddress);
        }
        else if (event->resourceType == MALLOC_FREE)
        {
            if(config->memAllocMap.find((uintptr_t) event->allocAddress) != config->memAllocMap.end())
            {
                //
                // If in the allocation map, remove the allocation
                //
                config->memAllocMap.erase((uintptr_t) event->allocAddress);
                Trace("Got event: free 0x%lx\n", event->allocAddress);
            }
        }
    }

    Trace("RestrackHandleEvent: Exit");
	return 0;
}

// ------------------------------------------------------------------------------------------
// ReportLeaks
//
// Reports on leaks
// ------------------------------------------------------------------------------------------
void ReportLeaks(ProcDumpConfiguration* config)
{
    void* symResolver = bcc_symcache_new(config->ProcessId, NULL);

    if(config->memAllocMap.size() > 0)
    {
        // TODO: Use something different for more efficient lookup
        std::vector<groupedAllocEntry> groupedAllocations;

        //
        // Group the call stacks
        //
        for (const auto& pair : config->memAllocMap)
        {
            bool found = false;
            for(int i=0; i<(int) groupedAllocations.size(); i++)
            {
                if(groupedAllocations[i].callStackLen == pair.second->callStackLen && (groupedAllocations[i].allocSize == pair.second->allocSize || groupedAllocations[i].allocSize == 0))
                {
                    bool match = true;
                    for(int j=0; j<pair.second->callStackLen; j++)
                    {
                        if(groupedAllocations[i].stackTrace[j] != pair.second->stackTrace[j])
                        {
                            match = false;
                            break;
                        }
                    }

                    if(match == true)
                    {
                        groupedAllocations[i].allocCount++;
                        groupedAllocations[i].totalAllocSize += pair.second->allocSize;
                        found = true;
                        break;
                    }
                }
            }

            if(found == false)
            {
                groupedAllocEntry entry = {};
                entry.type = pair.second->resourceType;
                entry.allocCount = 1;
                entry.allocSize = pair.second->allocSize;
                entry.totalAllocSize = pair.second->allocSize;
                entry.callStackLen = pair.second->callStackLen;
                memcpy(entry.stackTrace, pair.second->stackTrace, sizeof(__u64) * pair.second->callStackLen);
                groupedAllocations.push_back(entry);
            }
        }

        /*
        printf("---------------------------------------------------\n");
        printf("Grouped allocations:\n");
        for (const auto& pair : groupedAllocations)
        {
            printf("Allocations: %d Total Size: %d Call Stack Len: %d\n", pair.allocCount, pair.totalAllocSize, pair.callStackLen);
            for(unsigned int i=0; i<pair.callStackLen; i++)
            {
                //
                // Now we get the symbol information for the allocation call stacks that are outstanding.
                //
                // Below is just TMP.
                if(pair.stackTrace[i] > 0)
                {
                    printf("\t0x%llx\n", pair.stackTrace[i]);
                }
            }
        }
        printf("---------------------------------------------------\n");*/

        // Sort the vector based on the totalAllocSize field in ascending order
        std::sort(groupedAllocations.begin(), groupedAllocations.end(), [](const groupedAllocEntry& a, const groupedAllocEntry& b) {
            return a.totalAllocSize > b.totalAllocSize;
        });

        //
        // Print out the leaks
        //
        unsigned long totalLeak = 0;
        for (const auto& pair : groupedAllocations)
        {
            std::vector<stackFrame> callStack;
            for(unsigned int i=0; i<pair.callStackLen; i++)
            {
                //
                // Now we get the symbol information for the allocation call stacks that are outstanding.
                //
                if(pair.stackTrace[i] > 0)
                {
                    bcc_symbol sym;
                    if(bcc_symcache_resolve(symResolver, pair.stackTrace[i], &sym) != 0)
                    {
                        printf("Error resolving symbol for address: 0x%llx\n", pair.stackTrace[i]);
                        continue;
                    }

                    stackFrame frame = {};
                    frame.offset = sym.offset;
                    frame.symbolName = sym.name;
                    frame.demangledSymbolName = sym.demangle_name;
                    frame.pc = pair.stackTrace[i];

                    int len = snprintf(NULL, 0, "\t[0x%llx] %s+0x%lx\n", pair.stackTrace[i], frame.demangledSymbolName.c_str(), frame.offset);
                    frame.fullName = (char*) malloc(len+1);
                    snprintf(frame.fullName, len, "\t[0x%llx] %s+0x%lx\n", pair.stackTrace[i], frame.demangledSymbolName.c_str(), frame.offset);

                    callStack.push_back(frame);
                }
            }

            //
            // If the stack contains an ignore frame, don't print it
            //
            bool found = false;
            /*if(config.ignoreFrame != NULL)
            {
                for (const auto& st : callStack)
                {

                    if(strstr(st.fullName, config.ignoreFrame) != NULL)
                    {
                        found = true;
                        break;
                    }
                }
            }*/

            if(found == false)
            {
                totalLeak += pair.totalAllocSize;

                printf ("+++ Leaked Allocation [allocation size: 0x%lx count:0x%lx total size:0x%lx]\n", pair.allocSize, pair.allocCount, pair.totalAllocSize);

                switch(pair.type)
                {
                    case MALLOC_ALLOC:
                        printf("\tmalloc\n");
                        break;
                    case CALLOC_ALLOC:
                        printf("\tcalloc\n");
                        break;
                    case REALLOC_ALLOC:
                        printf("\trealloc\n");
                        break;
                    case REALLOCARRAY_ALLOC:
                        printf("\treallocarray\n");
                        break;
                    default:
                        printf("\tunknown\n");
                        break;
                }

                for (const auto& st : callStack)
                {
                    printf("\t[0x%llx] %s+0x%lx\n", st.pc, st.demangledSymbolName.c_str(), st.offset);
                }

                printf("\n");
            }
        }

        printf("\nTotal leaked: 0x%lx\n", totalLeak);
    }
    else
    {
        printf("No leaks detected.\n");
    }
}
