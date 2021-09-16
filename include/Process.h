// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// This library reads from the /procfs pseudo filesystem
//
//--------------------------------------------------------------------

#ifndef PROCFSLIB_PROCESS_H
#define PROCFSLIB_PROCESS_H

#include <linux/version.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "Logging.h"

// -----------------------------------------------------------
// a series of structs for containing infromation from /procfs
// -----------------------------------------------------------

// 
// /proc/[pid]/stat
//
struct ProcessStat {
    pid_t pid;      // the process ID : %d
    char *comm;     // The filename of the executable : %s
    char state;     // Process State, one of RSDZTtWXxKP : %c
    pid_t ppid;     // the parent process ID : %d
    pid_t pgrp;     // the process group ID of the process : %d
    int session;    // The session ID of the process : %d
    
    // The controlling terminal of the process. 
    // (the minor device number is contained in the combination 
    // of bits 31 to 20 and 7 to 0; the major device number is in bits 15 to 8) : %d
    int tty_nr; 
    pid_t tpgid; // The ID of the foreground process group of the controlling terminal process : %d

    unsigned int flags;  // Kernel flags word of the process. See PF_* definitions in include/linux/sched.h : %u

    // Number of minor faults the process has made which have not required loading a memory page from disk
    unsigned long minflt;

    // Number of minor faults that the process's waited-for children have made
    unsigned long cminflt;

    // Number of major faults the process has made which have required loading a memory page from disk
    unsigned long majflt;

    // Number of major faults that the process's waited-for children have made
    unsigned long cmajflt;

    // Amount of time that this process has been scheduled in user mode, measured in clock ticks
    unsigned long utime;

    // Amount of time that this process has been scheduled in kernel mode, measured in clock ticks
    unsigned long stime;

    // Amount of time that this process's waited-for children have been scheduled in user mode, measured in clock ticks
    unsigned long cutime;

    // Amount of time that this process's waited-for children have been scheduled in kernel mode, measured in clock ticks
    unsigned long cstime;

    // For processes running under a non-real-time scheduling policy, this is the raw
    // nice value (setpriority(2)) as represented in the kernel. For processes running in a
    // real-time scheduling policy, this is the negated scheduling priority minus one. 
    long priority;

    // The nice value, a value in the range 19 (low priority) to -20 (high priority)
    long nice;

    // Number of threads in this process
    long num_threads;

    // The time in jiffies before the next SIGALRM is sent to the process due to an interval timer.
    long itrealvalue;

    // The time the process started after system boot
    unsigned long long starttime;

    // Virtual memory size in bytes
    unsigned long vsize;

    // Resident Set Size: # of pages the process has in _real memory_.
    // This is just the pages which count toward text, data, or stack space.
    // This does not include pages which have not been demand-loaded in,
    // or which are swapped out
    long rss;

    // Current soft limit in bytes on the rss of the process
    unsigned long rsslim;

    //The address above which program text can run
    unsigned long startcode;

    //The address below which program text can run
    unsigned long endcode;

    // The address of the start (i.e., bottom) of the stack
    unsigned long startstack;

    // The current value of the ESP (stack pointer), as found in the kernel stack page for the process
    // NOTE: due to race conditions, this is not reliable, try ptrace
    unsigned long kstkesp;

    // The current EIP (Instruction pointer)
    // NOTE: due to race conditions, this is not reliable, try ptrace
    unsigned long kstkeip;

    // The bitmap of pending signals, displayed as a decimal number.
    // Obsolete, because it does not provide information on real-time signals;
    // for that use /proc/[pid]/status instead.
    unsigned long signal;

    // The bitmap of blocked signals, displayed as a decimal number;
    // Obsolete, because it does not provide information on real-time signals;
    // for that use /proc/[pid]/status instead.
    unsigned long blocked;

    // The bitmap of ignored signals, displayed as a decimal number.
    // Obsolete, because it does not provide information on real-time signals;
    // for that use /proc/[pid]/status instead.
    unsigned long sigignore;

    // The bitmap of caught signals, displayed as a decimal number.
    // Obsolete, because it does not provide information on real-time signals;
    // for that use /proc/[pid]/status instead.
    unsigned long sigcatch;

    // This is the "channel" in which the process is waiting.  It is the
    // address of a location in the kernel where the process is sleeping.
    // The corresponding symbolic name can be found in /proc/[pid]/wchan.
    unsigned long wchan;

    // Number of pages swapped (not maintained).
    unsigned long nswap; 

    // Cumulative nswap for child processes (not maintained).
    unsigned long cnswap;

    // signal to be sent to parent when we die. (since Linux 2.1.22)
    int exit_signal;

    // CPU number last executed on (since Linux 2.2.8)
    int processor;

    // Real-time scheduling priority, a number in the range 1 to 99 for
    // processes scheduled under a real-time policy, or 0, for non-real-
    // time processes 
    unsigned int rt_priority;

    // Scheduling policy. Decode using the SCHED_* constants in 
    // linux/sched.h
    unsigned int policy;

    // Aggregated block I/O delays, measured in clock ticks (centiseconds)
    unsigned long long delayacct_blkio_ticks;

    // Guest time of the process (time spent running a virtual CPU for a 
    // guest operating system), measured in clock ticks (divide by
    // sysconf(_SC_CLK_TCK)).
    unsigned long guest_time;

    // Guest time of the the process's children, measure in clock ticks
    // (divide by sysconf(_SC_CLK_TCK)).
    long cguest_time;

    // Address above which program initialized and uninitialized (BSS)
    // data are placed.
    unsigned long start_data;

    // Address below which program initialized and uninitialized (BSS)
    // data are placed.
    unsigned long end_data;

    // Address above which program heap can be expanded with brk.
    unsigned long start_brk;

    // Address above which program command-line arguments (argv) are placed.
    unsigned long arg_start;

    // Address below which program command-lin arguments (argv) are placed.
    unsigned long arg_end;

    // Address above which program environment is placed.
    unsigned long env_start;

    // Address below which program environment is placed.
    unsigned long env_end;

    // The thread'd exit status in the form reported by waitpid.
    int exit_code;    

    // NOTE: This does not come from /proc/[pid]/stat rather is populated by enumerating the /proc/<pid>>/fdinfo
    int num_filedescriptors;
};

//
// Struct for /proc/[pid]/status
//
struct ProcessStatus {
    char *Name;             // Command run by this process
    char State;             // Current state of the process. One of RSDTZX
    pid_t Tgid;             // Thread Group ID (i.e., Process ID).
    pid_t Pid;              // Thread ID
    pid_t PPid;             // PID of the parent process
    pid_t TracerPid;        // PID of the processtracing this process (0 if not being traced).
    uid_t Uid[4];           // Real [0], effective [1], saved set [2], and filesystem [3] UIDs
    pid_t Gid[4];           // Real [0], effective [1], saved set [2], and filesystem [3] GIDs
    int FDSize;             // Number of file descriptor slots currently allocated.
    pid_t *Groups;          // Supplementary group list (array).
    int GroupsLen;          
    unsigned long VmPeak;   // Peak virtual memory size.
    unsigned long VmSize;   // Virtual memory size.
    unsigned long VmLck;    // Locked virtual memory.
    unsigned long VmPin;    // Pinned memory size (since Linux 3.2).  These are pages that can't be moved because something needs to directly access physical memory.
    unsigned long VmHwM;    // Peak resident size ("High Water Mark").
    unsigned long VmRSS;    // Resident Set Size.
    unsigned long VmData;   // Size of data segment.
    unsigned long VmStk;    // Size of stack segment.
    unsigned long VmExe;    // Size of text segment.
    unsigned long VmLib;    // Shared library code size.
    unsigned long VmPTE;    // Page table entries size (since Linux 2.6.10).
    unsigned long VmPMD;    // Size of second-level page tables (since Linux 4.0).
    unsigned long VmSwap;   // Swapped-out virtual memory size by anonymous private pages; shmem swap usage is not included.
    int Threads;            // Number of threads in process containing this thread.

    // This  field  contains  two  slash-separated  numbers that relate to
    // queued signals for the real user ID of this process.  The first [0]  of  these
    // is the  number of currently queued signals for this real user ID, and the
    // second [1] is the resource limit on the number  of  queued  signals  for  this process
    int SigQ[2];

    unsigned long SigPnd; // Number of signals pending for thread.
    unsigned long ShdPnd; // Number of signals pending for process as a whole.

    unsigned long SigBlk; // Mask indicating signals being blocked.
    unsigned long SigIgn; // Mask indicating signals being ignored.
    unsigned long SigCgt; // Mask indicating signals being caught.

    unsigned long CapInh; // Mask of capabilities enabled in inheritable sets.
    unsigned long CapPrm; // Mask of capabilities enabled in permitted sets.
    unsigned long CapEff; // Mask of capabilities enabled in effective sets.
    unsigned long CapBnd; // Capability Bounding set.
    unsigned long CapAmb; // Ambient capability set (since linux 4.3).

    // Seccomp mode of the process (since Linux 3.8).
    // 0 means  SECCOMP_MODE_DISABLED;  
    // 1  means  SECCOMP_MODE_STRICT; 
    // 2 means SECCOMP_MODE_FILTER.  
    // This field is provided only if  the  kernel  was  built 
    // with the CONFIG_SECCOMP kernel configuration option enabled.
    int Seccomp;

    // These fields are subject to the formats laid out in cpuset(7)
    // They will be represented as char* here
    char *Cpus_allowed;        // Mask of CPUs on which this process may run.
    char *Cpus_allowed_list;   // Same as previous, but in "list format".
    char *Mems_allowed;        // Mask of memory nodes allowed to this process.
    char *Mems_allowed_list;   // Same as previous, but in "list format".

    int voluntary_ctxt_switches;       // Number of voluntary context switches.
    int nonvoluntary_ctxt_switches;    //Number of involuntary context switches.
};

// -----------------------------------------------------------
// a series of functions for collecting infromation from /procfs
// -----------------------------------------------------------

bool GetProcessStat(pid_t pid, struct ProcessStat *proc);

#endif // PROCFSLIB_PROCESS_H