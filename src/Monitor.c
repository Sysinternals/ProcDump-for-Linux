// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License

//--------------------------------------------------------------------
//
// Monitor functions
//
//--------------------------------------------------------------------
#include "Includes.h"

static pthread_t sig_thread_id;
static sigset_t sig_set;

TAILQ_HEAD(, ConfigQueueEntry) configQueueHead;
pthread_mutex_t queue_mutex;

extern struct ProcDumpConfiguration g_config;
extern struct ProcDumpConfiguration * target_config;

//--------------------------------------------------------------------
//
// SignalThread - Thread for handling graceful Async signals (e.g., SIGINT, SIGTERM)
//
//--------------------------------------------------------------------
void *SignalThread(void *input)
{
    int sig_caught, rc;
    struct ConfigQueueEntry * item;

    if ((rc = sigwait(&sig_set, &sig_caught)) != 0) {
        Log(error, "Failed to wait on signal");
        exit(-1);
    }

    switch (sig_caught)
    {
    case SIGINT:
        // In case of CTRL-C we need to iterate over all the outstanding monitors and handle them appropriately
        pthread_mutex_lock(&queue_mutex);
        TAILQ_FOREACH(item, &configQueueHead, element)
        {
            if(!IsQuit(item->config)) SetQuit(item->config, 1);

            if(item->config->gcorePid != NO_PID) {
                Log(info, "Shutting down gcore");
                if((rc = kill(-item->config->gcorePid, SIGKILL)) != 0) {            // pass negative PID to kill entire PGRP with value of gcore PID
                    Log(error, "Failed to shutdown gcore.");
                }
            }

            // Need to make sure we detach from ptrace (if not attached it will silently fail)
            // To avoid situations where we have intercepted a signal and CTRL-C is hit, we synchronize
            // access to the signal path (in SignalMonitoringThread). Note, there is still a race but
            // acceptable since it is very unlikely to occur. We also cancel the SignalMonitorThread to
            // break it out of waitpid call.
            if(item->config->SignalNumber != -1)
            {
                for(int i=0; i<item->config->nThreads; i++)
                {
                    if(item->config->Threads[i].trigger == Signal)
                    {
                        pthread_mutex_lock(&item->config->ptrace_mutex);
                        ptrace(PTRACE_DETACH, item->config->ProcessId, 0, 0);
                        pthread_mutex_unlock(&item->config->ptrace_mutex);

                        if ((rc = pthread_cancel(item->config->Threads[i].thread)) != 0) {
                            Log(error, "An error occurred while canceling SignalMonitorThread.\n");
                            exit(-1);
                        }
                    }
                }
            }
        }

        Log(info, "Quit");
        SetQuit(&g_config, 1);                  // Make sure to signal the global config
        pthread_mutex_unlock(&queue_mutex);
        break;

        default:
        fprintf (stderr, "\nUnexpected signal %d\n", sig_caught);
        break;
    }

    pthread_exit(NULL);
}

//--------------------------------------------------------------------
//
// MonitorProcesses
// MonitorProcess is the starting point of where the monitors get
// created. It uses a list to store all the monitors that are active.
// All monitors must go on this list as there are other places (for
// example, SignalThread) that relies on all active monitors to be part
// of the list. Any access to this list must be protected by queue_mutex.
//
//--------------------------------------------------------------------
void MonitorProcesses(struct ProcDumpConfiguration *self)
{
    if (self->WaitingForProcessName)    Log(info, "Waiting for processes '%s' to launch\n", self->ProcessName);
    if (self->bProcessGroup == true)    Log(info, "Monitoring processes of PGID '%d'\n", self->ProcessGroup);

    // allocate list of configs for process monitoring
    TAILQ_INIT(&configQueueHead);
    int numMonitoredProcesses = 0;
    struct ConfigQueueEntry * item;

    // create binary map to track processes we have already tracked and closed
    int maxPid = GetMaximumPID();
    if(maxPid < 0)
    {
        Log(error, INTERNAL_ERROR);
        Trace("Unable to get MAX_PID value\n");
        return;
    }

    struct MonitoredProcessMapEntry* monitoredProcessMap = (struct MonitoredProcessMapEntry*) calloc(maxPid, sizeof(struct MonitoredProcessMapEntry));
    if(!monitoredProcessMap)
    {
        Log(error, INTERNAL_ERROR);
        Trace("CreateTriggerThreads: failed to allocate memory for monitorProcessMap.");
        ExitProcDump();
        return;
    }

    // Create a signal handler thread where we handle shutdown as a result of SIGINT.
    // Note: We only create ONE per instance of procdump rather than per monitor.
    if((pthread_create(&sig_thread_id, NULL, SignalThread, (void *)self))!= 0)
    {
        Log(error, INTERNAL_ERROR);
        Trace("CreateTriggerThreads: failed to create SignalThread.");
        free(monitoredProcessMap);
        ExitProcDump();
        return;
    }

    Log(info, "\n\nPress Ctrl-C to end monitoring without terminating the process(es).\n");

    if(!self->WaitingForProcessName && !self->bProcessGroup)
    {
        //
        // Monitoring single process (-p)
        //

        //
        // Make sure target process exists
        //

        // If we have a process name find it to make sure it exists
        if(self->ProcessName)
        {
            if(!LookupProcessByName(self->ProcessName))
            {
                Log(error, "No process matching the specified name (%s) can be found.", self->ProcessName);
                return;
            }

            // Set the process ID so the monitor can target.
            self->ProcessId = LookupProcessPidByName(g_config.ProcessName);
        }
        else if (self->ProcessId != NO_PID && !LookupProcessByPid(self->ProcessId))
        {
            Log(error, "No process matching the specified PID (%d) can be found.", self->ProcessId);
            return;
        }

        self->ProcessName = GetProcessName(self->ProcessId);

        item = (struct ConfigQueueEntry*)malloc(sizeof(struct ConfigQueueEntry));
        item->config = CopyProcDumpConfiguration(self);

        if(item->config == NULL)
        {
            Log(error, INTERNAL_ERROR);
            Trace("MonitorProcesses: failed to alloc struct for process.");
            free(monitoredProcessMap);
            ExitProcDump();
            return;
        }

        // insert config into queue
        pthread_mutex_lock(&queue_mutex);
        TAILQ_INSERT_HEAD(&configQueueHead, item, element);
        monitoredProcessMap[item->config->ProcessId].active = true;
        pthread_mutex_unlock(&queue_mutex);

        // print config here
        PrintConfiguration(self);

        if(StartMonitor(self)!=0)
        {
            Log(error, INTERNAL_ERROR);
            Trace("MonitorProcesses: Failed to start the monitor.");
            free(monitoredProcessMap);
            ExitProcDump();
            return;
        }

        WaitForAllMonitorsToTerminate(self);
        Log(info, "Stopping monitor for process %s (%d)", self->ProcessName, self->ProcessId);
        WaitForSignalThreadToTerminate(self);

        pthread_mutex_lock(&queue_mutex);
        TAILQ_REMOVE(&configQueueHead, item, element);
        monitoredProcessMap[item->config->ProcessId].active = false;
        pthread_mutex_unlock(&queue_mutex);
        FreeProcDumpConfiguration(item->config);
        free(item);
    }
    else
    {
        // print config here
        PrintConfiguration(self);

        do
        {
            // Multi process monitoring

            // If we are monitoring for PGID, validate the root process exists
            if(self->bProcessGroup && !LookupProcessByPgid(self->ProcessGroup)) {
                Log(error, "No process matching the specified PGID can be found.");
                PrintUsage();
                return;
            }

            // Iterate over all running processes
            struct dirent ** nameList;
            int numEntries = scandir("/proc/", &nameList, FilterForPid, alphasort);
            for (int i = 0; i < numEntries; i++)
            {
                pid_t procPid;
                if(!ConvertToInt(nameList[i]->d_name, &procPid)) return;

                if(self->bProcessGroup)
                {
                    // We are monitoring a process group (-g)
                    pid_t pgid = GetProcessPgid(procPid);
                    if(pgid != NO_PID && pgid == self->ProcessGroup)
                    {
                        struct ProcessStat procStat;
                        bool ret = GetProcessStat(procPid, &procStat);

                        // Note: To solve the PID reuse case, we uniquely identify an entry via {PID}{starttime}
                        if(ret && (monitoredProcessMap[procPid].active == false || monitoredProcessMap[procPid].starttime != procStat.starttime))
                        {
                            // allocate for new queue entry
                            item = (struct ConfigQueueEntry*)malloc(sizeof(struct ConfigQueueEntry));
                            item->config = CopyProcDumpConfiguration(self);

                            if(item->config == NULL)
                            {
                                Log(error, INTERNAL_ERROR);
                                Trace("MonitorProcesses: failed to alloc struct for process.");
                                free(monitoredProcessMap);
                                ExitProcDump();
                                return;
                            }

                            // populate fields for this target
                            item->config->ProcessId = procPid;
                            item->config->ProcessName = GetProcessName(procPid);

                            // insert config into queue
                            pthread_mutex_lock(&queue_mutex);
                            TAILQ_INSERT_HEAD(&configQueueHead, item, element);
                            monitoredProcessMap[item->config->ProcessId].active = true;
                            monitoredProcessMap[item->config->ProcessId].starttime = procStat.starttime;
                            pthread_mutex_unlock(&queue_mutex);

                            if(StartMonitor(item->config)!=0)
                            {
                                Log(error, INTERNAL_ERROR);
                                Trace("MonitorProcesses: Failed to start the monitor.");
                                free(monitoredProcessMap);
                                ExitProcDump();
                                return;
                            }

                            numMonitoredProcesses++;
                        }
                    }
                }
                else if(self->WaitingForProcessName)
                {
                    // We are monitoring for a process name (-w)
                    char *nameForPid = GetProcessName(procPid);

                    // check to see if process name matches target
                    if (nameForPid && strcmp(nameForPid, self->ProcessName) == 0)
                    {
                        struct ProcessStat procStat;
                        bool ret = GetProcessStat(procPid, &procStat);

                        // Note: To solve the PID reuse case, we uniquely identify an entry via {PID}{starttime}
                        if(ret && (monitoredProcessMap[procPid].active == false || monitoredProcessMap[procPid].starttime != procStat.starttime))
                        {
                            // allocate for new queue entry
                            item = (struct ConfigQueueEntry*)malloc(sizeof(struct ConfigQueueEntry));
                            item->config = CopyProcDumpConfiguration(self);

                            if(item->config == NULL)
                            {
                                Log(error, INTERNAL_ERROR);
                                Trace("MonitorProcesses: failed to alloc struct for named process.");
                                free(monitoredProcessMap);
                                ExitProcDump();
                                return;
                            }

                            // populate fields for this target
                            item->config->ProcessId = procPid;
                            item->config->ProcessName = strdup(nameForPid);

                            // insert config into queue
                            pthread_mutex_lock(&queue_mutex);
                            TAILQ_INSERT_HEAD(&configQueueHead, item, element);
                            monitoredProcessMap[item->config->ProcessId].active = true;
                            monitoredProcessMap[item->config->ProcessId].starttime = procStat.starttime;
                            pthread_mutex_unlock(&queue_mutex);

                            if(StartMonitor(item->config)!=0)
                            {
                                Log(error, INTERNAL_ERROR);
                                Trace("MonitorProcesses: Failed to start the monitor.");
                                free(monitoredProcessMap);
                                ExitProcDump();
                                return;
                            }

                            numMonitoredProcesses++;
                        }
                    }
                }
            }

            // clean up namelist
            for (int i = 0; i < numEntries; i++)
            {
                free(nameList[i]);
            }
            free(nameList);

            // cleanup process configs for child processes that have exited or for monitors that have captured N dumps
            pthread_mutex_lock(&queue_mutex);

            // TALQ_FOREACH does not support changing the list while iterating.
            // We use a seperate delete list instead.
            int count = 0;
            TAILQ_FOREACH(item, &configQueueHead, element)
            {
                count++;
            }

            struct ConfigQueueEntry** deleteList = (struct ConfigQueueEntry**) malloc(count*(sizeof(struct ConfigQueueEntry*)));
            if(deleteList == NULL)
            {
                Log(error, INTERNAL_ERROR);
                Trace("WriteCoreDumpInternal: failed to allocate memory for deleteList");
                ExitProcDump();
                return;
            }

            count = 0;

            // Iterate over the queue and store the items to delete
            TAILQ_FOREACH(item, &configQueueHead, element)
            {
                // The conditions under which we stop monitoring a process are:
                // 1. Process has been terminated
                // 2. The monitoring thread has exited
                // 3. The monitor has collected the required number of dumps
                if(item->config->bTerminated || item->config->nQuit || item->config->NumberOfDumpsCollected == item->config->NumberOfDumpsToCollect)
                {
                    Log(info, "Stopping monitors for process: %s (%d)", item->config->ProcessName, item->config->ProcessId);
                    WaitForAllMonitorsToTerminate(item->config);

                    deleteList[count++] = item;

                    numMonitoredProcesses--;
                }
            }

            // Iterate over the delete list and actually delete the items from the queue
            for(int i=0; i<count; i++)
            {
                // free config entry
                FreeProcDumpConfiguration(deleteList[i]->config);
                TAILQ_REMOVE(&configQueueHead, deleteList[i], element);
                free(deleteList[i]);
            }
            free(deleteList);
            pthread_mutex_unlock(&queue_mutex);

            // Exit if we are monitoring PGID and there are no more processes to monitor.
            // If we are monitoring for processes based on a process name we keep monitoring
            if(numMonitoredProcesses == 0 && self->WaitingForProcessName == false)
            {
                break;
            }

            // Wait for the polling interval the user specified before we check again
            sleep(g_config.PollingInterval / 1000);

        // We keep iterating while we have processes to monitor (in case of -g <pgid>) or if process name has
        // been specified (-w) in which case we keep monitoring until CTRL-C or finally if we have a quit signal.
        } while ((numMonitoredProcesses >= 0 || self->WaitingForProcessName == true) && !IsQuit(&g_config));

        // cleanup monitoring queue
        pthread_mutex_lock(&queue_mutex);
        int count = 0;
        TAILQ_FOREACH(item, &configQueueHead, element)
        {
            count++;
        }

        struct ConfigQueueEntry** deleteList = (struct ConfigQueueEntry**) malloc(count*(sizeof(struct ConfigQueueEntry*)));
        if(deleteList == NULL)
        {
            Log(error, INTERNAL_ERROR);
            Trace("WriteCoreDumpInternal: failed to allocate memory for deleteList");
            free(monitoredProcessMap);
            ExitProcDump();
            return;
        }

        count = 0;

        TAILQ_FOREACH(item, &configQueueHead, element)
        {
            SetQuit(item->config, 1);
            WaitForAllMonitorsToTerminate(item->config);

            deleteList[count++] = item;
        }

        // Iterate over the delete list and actually delete the items from the queue
        for(int i=0; i<count; i++)
        {
            // free config entry
            FreeProcDumpConfiguration(deleteList[i]->config);
            TAILQ_REMOVE(&configQueueHead, deleteList[i], element);
            free(deleteList[i]);
        }
        free(deleteList);
        pthread_mutex_unlock(&queue_mutex);

        free(target_config);

    }
}


//--------------------------------------------------------------------
//
// CreateTriggerThreads - Create each of the threads that will be running as a trigger
//
//--------------------------------------------------------------------
int CreateTriggerThreads(struct ProcDumpConfiguration *self)
{
    int rc = 0;
    self->nThreads = 0;
    bool tooManyTriggers = false;

    if((rc=sigemptyset (&sig_set)) < 0)
    {
        Trace("CreateTriggerThreads: sigemptyset failed.");
        return rc;
    }
    if((rc=sigaddset (&sig_set, SIGINT)) < 0)
    {
        Trace("CreateTriggerThreads: sigaddset failed.");
        return rc;
    }
    if((rc=sigaddset (&sig_set, SIGTERM)) < 0)
    {
        Trace("CreateTriggerThreads: sigaddset failed.");
        return rc;
    }

    if((rc = pthread_sigmask (SIG_BLOCK, &sig_set, NULL)) != 0)
    {
        Trace("CreateTriggerThreads: pthread_sigmask failed.");
        return rc;
    }

    // create threads
    if (self->CpuThreshold != -1) {
        if (self->nThreads < MAX_TRIGGERS) {
            if ((rc = pthread_create(&self->Threads[self->nThreads].thread, NULL, CpuMonitoringThread, (void *)self)) != 0) {
                Trace("CreateTriggerThreads: failed to create CpuThread.");
                return rc;
            }

            self->Threads[self->nThreads].trigger = Processor;
            self->nThreads++;

        } else
            tooManyTriggers = true;
    }

    if (self->MemoryThreshold != -1 && !tooManyTriggers) {
        if (self->nThreads < MAX_TRIGGERS) {
            if ((rc = pthread_create(&self->Threads[self->nThreads].thread, NULL, CommitMonitoringThread, (void *)self)) != 0) {
                Trace("CreateTriggerThreads: failed to create CommitThread.");
                return rc;
            }

            self->Threads[self->nThreads].trigger = Commit;
            self->nThreads++;

        } else
            tooManyTriggers = true;
    }

    if (self->ThreadThreshold != -1 && !tooManyTriggers) {
        if (self->nThreads < MAX_TRIGGERS) {
            if ((rc = pthread_create(&self->Threads[self->nThreads].thread, NULL, ThreadCountMonitoringThread, (void *)self)) != 0) {
                Trace("CreateTriggerThreads: failed to create ThreadThread.");
                return rc;
            }

            self->Threads[self->nThreads].trigger = ThreadCount;
            self->nThreads++;

        } else
            tooManyTriggers = true;
    }

    if (self->FileDescriptorThreshold != -1 && !tooManyTriggers) {
        if (self->nThreads < MAX_TRIGGERS) {
            if ((rc = pthread_create(&self->Threads[self->nThreads].thread, NULL, FileDescriptorCountMonitoringThread, (void *)self)) != 0) {
                Trace("CreateTriggerThreads: failed to create FileDescriptorThread.");
                return rc;
            }

            self->Threads[self->nThreads].trigger = FileDescriptorCount;
            self->nThreads++;
        } else
            tooManyTriggers = true;
    }

    if (self->SignalNumber != -1 && !tooManyTriggers) {
        if ((rc = pthread_create(&self->Threads[self->nThreads].thread, NULL, SignalMonitoringThread, (void *)self)) != 0) {
            Trace("CreateTriggerThreads: failed to create SignalMonitoringThread.");
            return rc;
        }

        self->Threads[self->nThreads].trigger = Signal;
        self->nThreads++;
    }

    if (self->bTimerThreshold && !tooManyTriggers) {
        if (self->nThreads < MAX_TRIGGERS) {
            if ((rc = pthread_create(&self->Threads[self->nThreads].thread, NULL, TimerThread, (void *)self)) != 0) {
                Trace("CreateTriggerThreads: failed to create TimerThread.");
                return rc;
            }

            self->Threads[self->nThreads].trigger = Timer;
            self->nThreads++;
        } else
            tooManyTriggers = true;
    }

    if (tooManyTriggers)
    {
        Log(error, "Too many triggers.  ProcDump only supports up to %d triggers.", MAX_TRIGGERS);
        return -1;
    }

    return 0;
}

//--------------------------------------------------------------------
//
// StartMonitor
// Creates the monitoring threads and begins the monitor based on
// the configuration passed in. In the case of exception monitoring
// we inject the monitor into the target process.
//
//--------------------------------------------------------------------
int StartMonitor(struct ProcDumpConfiguration* monitorConfig)
{
    int ret = 0;

    if(monitorConfig->bDumpOnException)
    {
        // Inject the profiler into the target process
        if(InjectProfiler(monitorConfig)!=0)
        {
            Log(error, "Failed to inject profiler into target process. Please make sure the target process is a .NET process");
            Trace("StartMonitor: failed to inject profiler into target process.");
            return -1;
        }
    }
    else
    {
        if(CreateTriggerThreads(monitorConfig) != 0)
        {
            Log(error, INTERNAL_ERROR);
            Trace("StartMonitor: failed to create trigger threads.");
            return -1;
        }
    }

    if(BeginMonitoring(monitorConfig) == false)
    {
        Log(error, INTERNAL_ERROR);
        Trace("StartMonitor: failed to start monitoring.");
        return -1;
    }

    Log(info, "Starting monitor for process %s (%d)", monitorConfig->ProcessName, monitorConfig->ProcessId);

    return ret;
}

//--------------------------------------------------------------------
//
// WaitForQuit - Wait for Quit Event or just timeout
//
//      Timed wait with awareness of quit event
//
// Returns: WAIT_OBJECT_0   - Quit triggered
//          WAIT_TIMEOUT    - Timeout
//          WAIT_ABANDONED  - At dump limit or terminated
//
//--------------------------------------------------------------------
int WaitForQuit(struct ProcDumpConfiguration *self, int milliseconds)
{
    if (!ContinueMonitoring(self)) {
        return WAIT_ABANDONED;
    }

    int wait = WaitForSingleObject(&self->evtQuit, milliseconds);

    if ((wait == WAIT_TIMEOUT) && !ContinueMonitoring(self)) {
        return WAIT_ABANDONED;
    }

    return wait;
}

//--------------------------------------------------------------------
//
// WaitForQuitOrEvent - Wait for Quit Event, an Event, or just timeout
//
//      Use to wait for dumps to complete, yet be aware of quit or finished events
//
// Returns: WAIT_OBJECT_0   - Quit triggered
//          WAIT_OBJECT_0+1 - Event triggered
//          WAIT_TIMEOUT    - Timeout
//          WAIT_ABANDONED  - (Abandonded) At dump limit or terminated
//
//--------------------------------------------------------------------
int WaitForQuitOrEvent(struct ProcDumpConfiguration *self, struct Handle *handle, int milliseconds)
{
    struct Handle *waits[2];
    waits[0] = &self->evtQuit;
    waits[1] = handle;

    if (!ContinueMonitoring(self)) {
        return WAIT_ABANDONED;
    }

    int wait = WaitForMultipleObjects(2, waits, false, milliseconds);
    if ((wait == WAIT_TIMEOUT) && !ContinueMonitoring(self)) {
        return WAIT_ABANDONED;
    }

    if ((wait == WAIT_OBJECT_0) && !ContinueMonitoring(self)) {
        return WAIT_ABANDONED;
    }

    return wait;
}

//--------------------------------------------------------------------
//
// WaitForAllMonitorsToTerminate - Wait for all monitors to terminate
//
//--------------------------------------------------------------------
int WaitForAllMonitorsToTerminate(struct ProcDumpConfiguration *self)
{
    int rc = 0;

    if(self->bDumpOnException)
    {
        // If we are monitoring for exceptions, we don't have any threads per se,
        // rather we wait for the profiler to let us know once done.

        // TODO: WAIT FOR PROFILER
    }
    else
    {
        // Wait for the other monitoring threads
        for (int i = 0; i < self->nThreads; i++) {
            if ((rc = pthread_join(self->Threads[i].thread, NULL)) != 0) {
                Log(error, "An error occurred while joining threads\n");
                exit(-1);
            }
        }
    }

    return rc;
}


//--------------------------------------------------------------------
//
// WaitForSignalThreadToTerminate - Wait for signal handler thread to terminate
//
//--------------------------------------------------------------------
int WaitForSignalThreadToTerminate(struct ProcDumpConfiguration *self)
{
    int rc = 0;

    // Cancel the signal handling thread.
    // We dont care about the return since the signal thread might already be gone.
    pthread_cancel(sig_thread_id);

    // Wait for signal handling thread to complete
    if ((rc = pthread_join(sig_thread_id, NULL)) != 0) {
        Log(error, "An error occurred while joining SignalThread.\n");
        exit(-1);
    }

    return rc;
}

//--------------------------------------------------------------------
//
// IsQuit - A check on the underlying value of whether we should quit
//
//--------------------------------------------------------------------
bool IsQuit(struct ProcDumpConfiguration *self)
{
    return (self->nQuit != 0);
}


//--------------------------------------------------------------------
//
// SetQuit - Sets the quit value and signals the event
//
//--------------------------------------------------------------------
int SetQuit(struct ProcDumpConfiguration *self, int quit)
{
    self->nQuit = quit;
    SetEvent(&self->evtQuit.event);

    return self->nQuit;
}

//--------------------------------------------------------------------
//
// ContinueMonitoring - Should we keep monitoring or should we clean up our thread
//
//--------------------------------------------------------------------
bool ContinueMonitoring(struct ProcDumpConfiguration *self)
{
    // Have we reached the dump limit?
    if (self->NumberOfDumpsCollected >= self->NumberOfDumpsToCollect) {
        return false;
    }

    // Do we already know the process is terminated?
    if (self->bTerminated) {
        return false;
    }

    // check if any process are running with PGID
    if(self->bProcessGroup && kill(-1 * self->ProcessGroup, 0)) {
        self->bTerminated = true;
        return false;
    }

    // Let's check to make sure the process is still alive then
    // note: kill([pid], 0) doesn't send a signal but does perform error checking
    //       therefore, if it returns 0, the process is still alive, -1 means it errored out
    if (self->ProcessId != NO_PID && kill(self->ProcessId, 0)) {
        self->bTerminated = true;
        Log(warn, "Target process %d is no longer alive", self->ProcessId);
        return false;
    }

    // Otherwise, keep going!
    return true;
}

//--------------------------------------------------------------------
//
// BeginMonitoring - Sync up monitoring threads
//
//--------------------------------------------------------------------
bool BeginMonitoring(struct ProcDumpConfiguration *self)
{
    return SetEvent(&(self->evtStartMonitoring.event));
}

extern long HZ;                                // clock ticks per second


//--------------------------------------------------------------------
//
// CommitMonitoringThread - Thread monitoring for memory consumption
//
//--------------------------------------------------------------------
void *CommitMonitoringThread(void *thread_args /* struct ProcDumpConfiguration* */)
{
    Trace("CommitMonitoringThread: Starting Trigger Thread");
    struct ProcDumpConfiguration *config = (struct ProcDumpConfiguration *)thread_args;

    long pageSize_kb;
    unsigned long memUsage = 0;
    struct ProcessStat proc = {0};
    int rc = 0;
    struct CoreDumpWriter *writer = NewCoreDumpWriter(COMMIT, config);

    pageSize_kb = sysconf(_SC_PAGESIZE) >> 10; // convert bytes to kilobytes (2^10)

    if ((rc = WaitForQuitOrEvent(config, &config->evtStartMonitoring, INFINITE_WAIT)) == WAIT_OBJECT_0 + 1)
    {
        while ((rc = WaitForQuit(config, config->PollingInterval)) == WAIT_TIMEOUT)
        {
            if (GetProcessStat(config->ProcessId, &proc))
            {
                // Calc Commit
                memUsage = (proc.rss * pageSize_kb) >> 10;    // get Resident Set Size
                memUsage += (proc.nswap * pageSize_kb) >> 10; // get Swap size

                // Commit Trigger
                if ((config->bMemoryTriggerBelowValue && (memUsage < config->MemoryThreshold)) ||
                    (!config->bMemoryTriggerBelowValue && (memUsage >= config->MemoryThreshold)))
                {
                    Log(info, "Trigger: Commit usage:%ldMB on process ID: %d", memUsage, config->ProcessId);
                    rc = WriteCoreDump(writer);
                    if(rc != 0)
                    {
                        SetQuit(config, 1);
                    }

                    if ((rc = WaitForQuit(config, config->ThresholdSeconds * 1000)) != WAIT_TIMEOUT)
                    {
                        break;
                    }
                }
            }
            else
            {
                Log(error, "An error occurred while parsing procfs\n");
                exit(-1);
            }
        }
    }

    free(writer);
    Trace("CommitMonitoringThread: Exiting Trigger Thread");
    pthread_exit(NULL);
}

//--------------------------------------------------------------------
//
// ThreadCountMonitoringThread - Thread monitoring for thread count
//
//--------------------------------------------------------------------
void* ThreadCountMonitoringThread(void *thread_args /* struct ProcDumpConfiguration* */)
{
    Trace("ThreadCountMonitoringThread: Starting Thread Thread");
    struct ProcDumpConfiguration *config = (struct ProcDumpConfiguration *)thread_args;

    struct ProcessStat proc = {0};
    int rc = 0;
    struct CoreDumpWriter *writer = NewCoreDumpWriter(THREAD, config);

    if ((rc = WaitForQuitOrEvent(config, &config->evtStartMonitoring, INFINITE_WAIT)) == WAIT_OBJECT_0 + 1)
    {
        while ((rc = WaitForQuit(config, config->PollingInterval)) == WAIT_TIMEOUT)
        {
            if (GetProcessStat(config->ProcessId, &proc))
            {
                if (proc.num_threads >= config->ThreadThreshold)
                {
                    Log(info, "Trigger: Thread count:%ld on process ID: %d", proc.num_threads, config->ProcessId);
                    rc = WriteCoreDump(writer);
                    if(rc != 0)
                    {
                        SetQuit(config, 1);
                    }

                    if ((rc = WaitForQuit(config, config->ThresholdSeconds * 1000)) != WAIT_TIMEOUT)
                    {
                        break;
                    }
                }
            }
            else
            {
                Log(error, "An error occurred while parsing procfs\n");
                exit(-1);
            }
        }
    }

    free(writer);
    Trace("ThreadCountMonitoringThread: Exiting Thread trigger Thread");
    pthread_exit(NULL);
}


//--------------------------------------------------------------------
//
// FileDescriptorCountMonitoringThread - Thread monitoring for file
// descriptor count
//
//--------------------------------------------------------------------
void* FileDescriptorCountMonitoringThread(void *thread_args /* struct ProcDumpConfiguration* */)
{
    Trace("FileDescriptorCountMonitoringThread: Starting Filedescriptor Thread");
    struct ProcDumpConfiguration *config = (struct ProcDumpConfiguration *)thread_args;

    struct ProcessStat proc = {0};
    int rc = 0;
    struct CoreDumpWriter *writer = NewCoreDumpWriter(FILEDESC, config);

    if ((rc = WaitForQuitOrEvent(config, &config->evtStartMonitoring, INFINITE_WAIT)) == WAIT_OBJECT_0 + 1)
    {
        while ((rc = WaitForQuit(config, config->PollingInterval)) == WAIT_TIMEOUT)
        {
            if (GetProcessStat(config->ProcessId, &proc))
            {
                if (proc.num_filedescriptors >= config->FileDescriptorThreshold)
                {
                    Log(info, "Trigger: File descriptors:%ld on process ID: %d", proc.num_filedescriptors, config->ProcessId);
                    rc = WriteCoreDump(writer);
                    if(rc != 0)
                    {
                        SetQuit(config, 1);
                    }

                    if ((rc = WaitForQuit(config, config->ThresholdSeconds * 1000)) != WAIT_TIMEOUT)
                    {
                        break;
                    }
                }
            }
            else
            {
                Log(error, "An error occurred while parsing procfs\n");
                exit(-1);
            }
        }
    }

    free(writer);
    Trace("FileDescriptorCountMonitoringThread: Exiting Filedescriptor trigger Thread");
    pthread_exit(NULL);
}

//
// This thread monitors for a specific signal to be sent to target process.
// It uses ptrace (PTRACE_SEIZE) and once the signal with the corresponding
// signal number is intercepted, it detaches from the target process in a stopped state
// followed by invoking gcore to generate the dump. Once completed, a SIGCONT followed by the
// original signal is sent to the target process. Signals of non-interest are simply forwarded
// to the target process.
//
// Polling interval has no meaning during signal monitoring.
//
void* SignalMonitoringThread(void *thread_args /* struct ProcDumpConfiguration* */)
{
    Trace("SignalMonitoringThread: Starting SignalMonitoring Thread");
    struct ProcDumpConfiguration *config = (struct ProcDumpConfiguration *)thread_args;
    int wstatus;
    int signum=-1;
    int rc = 0;
    int dumpStatus = 0;
    struct CoreDumpWriter *writer = NewCoreDumpWriter(SIGNAL, config);

    if ((rc = WaitForQuitOrEvent(config, &config->evtStartMonitoring, INFINITE_WAIT)) == WAIT_OBJECT_0 + 1)
    {
        // Attach to the target process. We use SEIZE here to avoid
        // the SIGSTOP issues of the ATTACH method.
        if (ptrace(PTRACE_SEIZE, config->ProcessId, NULL, NULL) == -1)
        {
            Log(error, "Unable to PTRACE the target process");
        }
        else
        {
            while(1)
            {
                // Wait for signal to be delivered
                waitpid(config->ProcessId, &wstatus, 0);
                if(WIFEXITED(wstatus) || WIFSIGNALED(wstatus))
                {
                    ptrace(PTRACE_DETACH, config->ProcessId, 0, 0);
                    break;
                }

                pthread_mutex_lock(&config->ptrace_mutex);

                // We are now in a signal-stop state

                signum = WSTOPSIG(wstatus);
                if(signum == config->SignalNumber)
                {
                    // We have to detach in a STOP state so we can invoke gcore
                    if(ptrace(PTRACE_DETACH, config->ProcessId, 0, SIGSTOP) == -1)
                    {
                        Log(error, "Unable to PTRACE (DETACH) the target process");
                        pthread_mutex_unlock(&config->ptrace_mutex);
                        break;
                    }

                    // Write core dump
                    Log(info, "Trigger: Signal:%d on process ID: %d", signum, config->ProcessId);
                    dumpStatus = WriteCoreDump(writer);
                    if(dumpStatus != 0)
                    {
                        SetQuit(config, 1);
                    }

                    kill(config->ProcessId, SIGCONT);

                    if(config->NumberOfDumpsCollected >= config->NumberOfDumpsToCollect)
                    {
                        // If we are over the max number of dumps to collect, send the original signal we intercepted.
                        kill(config->ProcessId, signum);
                        pthread_mutex_unlock(&config->ptrace_mutex);
                        break;
                    }

                    ptrace(PTRACE_CONT, config->ProcessId, NULL, signum);

                    // Re-attach to the target process
                    if (ptrace(PTRACE_SEIZE, config->ProcessId, NULL, NULL) == -1)
                    {
                        Log(error, "Unable to PTRACE the target process");
                        pthread_mutex_unlock(&config->ptrace_mutex);
                        break;
                    }

                    pthread_mutex_unlock(&config->ptrace_mutex);
                    continue;
                }

                // Resume execution of the target process
                ptrace(PTRACE_CONT, config->ProcessId, NULL, signum);
                pthread_mutex_unlock(&config->ptrace_mutex);

                if(dumpStatus != 0)
                {
                    break;
                }
            }
        }
    }

    free(writer);
    Trace("SignalMonitoringThread: Exiting SignalMonitoring Thread");
    pthread_exit(NULL);
}

//--------------------------------------------------------------------
//
// CpuMonitoringThread - Thread monitoring for CPU usage.
//
//--------------------------------------------------------------------
void *CpuMonitoringThread(void *thread_args /* struct ProcDumpConfiguration* */)
{
    Trace("CpuMonitoringThread: Starting Trigger Thread");
    struct ProcDumpConfiguration *config = (struct ProcDumpConfiguration *)thread_args;

    unsigned long totalTime = 0;
    unsigned long elapsedTime = 0;
    struct sysinfo sysInfo;
    int cpuUsage;
    struct CoreDumpWriter *writer = NewCoreDumpWriter(CPU, config);

    int rc = 0;
    struct ProcessStat proc = {0};

    if ((rc = WaitForQuitOrEvent(config, &config->evtStartMonitoring, INFINITE_WAIT)) == WAIT_OBJECT_0 + 1)
    {
        while ((rc = WaitForQuit(config, config->PollingInterval)) == WAIT_TIMEOUT)
        {
            sysinfo(&sysInfo);

            if (GetProcessStat(config->ProcessId, &proc))
            {
                // Calc CPU
                totalTime = (unsigned long)((proc.utime + proc.stime) / HZ);
                elapsedTime = (unsigned long)(sysInfo.uptime - (long)(proc.starttime / HZ));
                cpuUsage = (int)(100 * ((double)totalTime / elapsedTime));

                // CPU Trigger
                if ((config->bCpuTriggerBelowValue && (cpuUsage < config->CpuThreshold)) ||
                    (!config->bCpuTriggerBelowValue && (cpuUsage >= config->CpuThreshold)))
                {
                    Log(info, "Trigger: CPU usage:%d%% on process ID: %d", cpuUsage, config->ProcessId);
                    rc = WriteCoreDump(writer);
                    if(rc != 0)
                    {
                        SetQuit(config, 1);
                    }

                    if ((rc = WaitForQuit(config, config->ThresholdSeconds * 1000)) != WAIT_TIMEOUT)
                    {
                        break;
                    }
                }
            }
            else
            {
                Log(error, "An error occurred while parsing procfs\n");
                exit(-1);
            }
        }
    }

    free(writer);
    Trace("CpuTCpuMonitoringThread: Exiting Trigger Thread");
    pthread_exit(NULL);
}

//--------------------------------------------------------------------
//
// TimerThread - Thread that creates dumps based on specified timer
// interval.
//
//--------------------------------------------------------------------
void *TimerThread(void *thread_args /* struct ProcDumpConfiguration* */)
{
    Trace("TimerThread: Starting Trigger Thread");

    struct ProcDumpConfiguration *config = (struct ProcDumpConfiguration *)thread_args;
    struct CoreDumpWriter *writer = NewCoreDumpWriter(TIME, config);

    int rc = 0;

    if ((rc = WaitForQuitOrEvent(config, &config->evtStartMonitoring, INFINITE_WAIT)) == WAIT_OBJECT_0 + 1)
    {
        while ((rc = WaitForQuit(config, 0)) == WAIT_TIMEOUT)
        {
            Log(info, "Trigger: Timer:%ld(s) on process ID: %d", config->PollingInterval/1000, config->ProcessId);
            rc = WriteCoreDump(writer);
            if(rc != 0)
            {
                SetQuit(config, 1);
            }

            if ((rc = WaitForQuit(config, config->ThresholdSeconds * 1000)) != WAIT_TIMEOUT) {
                break;
            }
        }
    }

    free(writer);
    Trace("TimerThread: Exiting Trigger Thread");
    pthread_exit(NULL);
}
