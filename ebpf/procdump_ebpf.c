/*
    ProcDump for Linux

    Copyright (c) Microsoft Corporation

    All rights reserved.

    MIT License

    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the ""Software""), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "procdump_ebpf.h"
#include "procdump_ebpf_common.h"

pid_t target_PID;
int sampleRate;
int currentSampleCount;

char LICENSE[] SEC("license") = "Dual BSD/GPL";

// ------------------------------------------------------------------------------------------
// CheckSampleRate
//
// Returns true if we should sample this event.
// ------------------------------------------------------------------------------------------
__attribute__((always_inline))
static inline bool CheckSampleRate()
{
    if(currentSampleCount == sampleRate)
    {
        currentSampleCount = 1;
        return true;
    }
    else
    {
        currentSampleCount++;
        return false;
    }
}

// ------------------------------------------------------------------------------------------
// ZeroMemory
//
// We could just simply use memset but string.h is not working correctly due to needing
// the 32bit version of stubs.h (stubs-32.h). For some reason targeting bpf undefines __x86_64__.
// We can work around this on most architectures (they all have 32bit version) but not on
// mariner which does not offer it (for now).
// ------------------------------------------------------------------------------------------
__attribute__((always_inline))
static inline void ZeroMemory(char* ptr)
{
    for(int i = 0; i < sizeof(struct ResourceInformation); i++)
    {
        ptr[i] = 0;
    }
}

// ------------------------------------------------------------------------------------------
// uprobe_malloc
//
// This is the entry point for the malloc uprobe. It's called when malloc is called.
// ------------------------------------------------------------------------------------------
SEC("uprobe/libc.so.6:malloc")
int BPF_KPROBE(uprobe_malloc, long size)
{
    struct ResourceInformation* event;
    int element = 0;
    long ret = 0;
    uint64_t pidTid = bpf_get_current_pid_tgid();

    //
    // Only trace PIDs that matched the target PID
    //
    uint64_t pid = pidTid >> 32;
    if (pid != target_PID || CheckSampleRate() == false)
    {
        return 1;
    }

    //
    // Get heap element from the map
    //
    event = bpf_map_lookup_elem(&heapStorage, &element);
	if (event == NULL)
    {
		return 1;
    }

    ZeroMemory((char*) event);

    //
    // Setup the event we want to transfer to usermode.
    // Note that we use a callstack ID instead of the actual callstack.
    // The call to bpf_get_stackid populates the specified map with the
    // actual call stack.
    //
    event->allocSize = size;
    event->pid = target_PID;
    event->resourceType = MALLOC_ALLOC;
    event->allocAddress = 0;

    //
    // Update the arguments hashmap with the entry. We'll fetch the entry when
    // we exit the malloc and update other fields (such as allocation ptr) and then
    // send to user mode.
    //
    if ((ret = bpf_map_update_elem(&argsHashMap, &pidTid, event, BPF_ANY)) != 0)
    {
        BPF_PRINTK("Failed to update arguments hashmap\n");
        return 1;
    }

    BPF_PRINTK("malloc called with size = %d, target PID = %d, stacklen = %d\n", size, target_PID, event->callStackLen);
	return 0;
}

// ------------------------------------------------------------------------------------------
// uretprobe_malloc
//
// This is the entry point for the malloc exit uprobe. It's called when malloc is exiting.
// ------------------------------------------------------------------------------------------
SEC("uretprobe/libc.so.6:malloc")
int BPF_KRETPROBE(uretprobe_malloc, void* ret)
{
    uint64_t pidTid = bpf_get_current_pid_tgid();
    struct ResourceInformation* event;

    //
    // Only trace PIDs that matched the target PID
    //
    uint64_t p = pidTid >> 32;
    if (p != target_PID)
    {
        return 1;
    }

    //
    // Get the map storage for event.
    // This was created on the preceding malloc enter probe.
    // If the pid is in our map then we must have stored it
    //
    event = (struct ResourceInformation*) bpf_map_lookup_elem(&argsHashMap, &pidTid);
    if (event == NULL)
    {
        return 1;
    }

    event->allocAddress = (unsigned long) ret;
    event->callStackLen = bpf_get_stack(ctx, event->stackTrace, sizeof(event->stackTrace), BPF_F_USER_STACK) / sizeof(__u64);

    //
    // Send the event to the ring buffer (user land)
    //
	bpf_ringbuf_output(&ringBuffer, event, sizeof(*event), 0);

    //
    // Cleanup args hashmap entry
    //
    bpf_map_delete_elem(&argsHashMap, &pidTid);
    return 0;
}


// ------------------------------------------------------------------------------------------
// uprobe_free
//
// This is the entry point for the free uprobe. It's called when free is entered.
// ------------------------------------------------------------------------------------------
SEC("uprobe/libc.so.6:free")
int BPF_KRETPROBE(uprobe_free, void* alloc)
{
    struct ResourceInformation* event;
    int element = 1;
    long ret = 0;
    uint64_t pidTid = bpf_get_current_pid_tgid();

    //
    // Only trace PIDs that matched the target PID
    //
    uint64_t pid = pidTid >> 32;
    if (pid != target_PID)
    {
        return 1;
    }

    //
    // Get heap element from the map
    //
    event = bpf_map_lookup_elem(&heapStorage, &element);
	if (event == NULL)
    {
		return 1;
    }

    ZeroMemory((char*) event);

    //
    // Setup the event we want to transfer to usermode.
    //
    event->pid = target_PID;
    event->resourceType = MALLOC_FREE;
    event->allocAddress = (unsigned long) alloc;

    //
    // Update the arguments hashmap with the entry. We'll fetch the entry when
    // we free the allocation and update other fields (such as allocation ptr) and then
    // send to user mode.
    //
    if ((ret = bpf_map_update_elem(&argsHashMap, &pidTid, event, BPF_ANY)) != 0)
    {
        BPF_PRINTK("Failed to update arguments hashmap\n");
        return 1;
    }

    BPF_PRINTK("Free exiting with pid=%d, alloc=0x%p\n", target_PID, alloc);
	return 0;
}


// ------------------------------------------------------------------------------------------
// uretprobe_free
//
// This is the entry point for the free exit uprobe. It's called when free is exiting.
// ------------------------------------------------------------------------------------------
SEC("uretprobe/libc.so.6:free")
int BPF_KRETPROBE(uretprobe_free, void* ret)
{
    uint64_t pidTid = bpf_get_current_pid_tgid();
    struct ResourceInformation* event;

    //
    // Only trace PIDs that matched the target PID
    //
    uint64_t p = pidTid >> 32;
    if (p != target_PID)
    {
        return 1;
    }

    //
    // Get the map storage for event.
    // This was created on the preceding free enter probe.
    // If the pid is in our map then we must have stored it
    //
    event = (struct ResourceInformation*) bpf_map_lookup_elem(&argsHashMap, &pidTid);
    if (event == NULL)
    {
        return 1;
    }

    BPF_PRINTK("free exit returned with alloc = 0x%lx, target PID = %d\n", event->allocAddress, target_PID);

    //
    // Send the event to the ring buffer (user land)
    //
	bpf_ringbuf_output(&ringBuffer, event, sizeof(*event), 0);

    //
    // Cleanup args hashmap entry
    //
    bpf_map_delete_elem(&argsHashMap, &pidTid);
    return 0;
}

// ------------------------------------------------------------------------------------------
// uprobe_cmalloc
//
// This is the entry point for the calloc uprobe. It's called when calloc is called.
// ------------------------------------------------------------------------------------------
SEC("uprobe/libc.so.6:calloc")
int BPF_KPROBE(uprobe_calloc, int count, long size)
{
    struct ResourceInformation* event;
    int element = 0;
    long ret = 0;
    uint64_t pidTid = bpf_get_current_pid_tgid();

    //
    // Only trace PIDs that matched the target PID
    //
    uint64_t pid = pidTid >> 32;
    if (pid != target_PID || CheckSampleRate() == false)
    {
        return 1;
    }

    //
    // Get heap element from the map
    //
    event = bpf_map_lookup_elem(&heapStorage, &element);
	if (event == NULL)
    {
		return 1;
    }

    ZeroMemory((char*) event);

    //
    // Setup the event we want to transfer to usermode.
    // The call to bpf_get_stack populates the specified map with the
    // actual call stack.
    //
    event->allocSize = size * count;
    event->pid = target_PID;
    event->resourceType = CALLOC_ALLOC;
    event->allocAddress = 0;

    //
    // Update the arguments hashmap with the entry. We'll fetch the entry when
    // we exit the calloc and update other fields (such as allocation ptr) and then
    // send to user mode.
    //
    if ((ret = bpf_map_update_elem(&argsHashMap, &pidTid, event, BPF_ANY)) != 0)
    {
        BPF_PRINTK("Failed to update arguments hashmap\n");
        return 1;
    }

	return 0;
}

// ------------------------------------------------------------------------------------------
// uretprobe_cmalloc
//
// This is the entry point for the calloc exit uprobe. It's called when calloc is exiting.
// ------------------------------------------------------------------------------------------
SEC("uretprobe/libc.so.6:calloc")
int BPF_KRETPROBE(uretprobe_calloc, void* ret)
{
    uint64_t pidTid = bpf_get_current_pid_tgid();
    struct ResourceInformation* event;

    //
    // Only trace PIDs that matched the target PID
    //
    uint64_t p = pidTid >> 32;
    if (p != target_PID)
    {
        return 1;
    }

    //
    // Get the map storage for event.
    // This was created on the preceding calloc enter probe.
    // If the pid is in our map then we must have stored it
    //
    event = (struct ResourceInformation*) bpf_map_lookup_elem(&argsHashMap, &pidTid);
    if (event == NULL)
    {
        return 1;
    }

    event->allocAddress = (unsigned long) ret;
    event->callStackLen = bpf_get_stack(ctx, event->stackTrace, sizeof(event->stackTrace), BPF_F_USER_STACK) / sizeof(__u64);

    //
    // Send the event to the ring buffer (user land)
    //
	bpf_ringbuf_output(&ringBuffer, event, sizeof(*event), 0);

    //
    // Cleanup args hashmap entry
    //
    bpf_map_delete_elem(&argsHashMap, &pidTid);
    return 0;
}

// ------------------------------------------------------------------------------------------
// uprobe_realloc
//
// This is the entry point for the realloc uprobe. It's called when realloc is called.
// ------------------------------------------------------------------------------------------
SEC("uprobe/libc.so.6:realloc")
int BPF_KPROBE(uprobe_realloc, void* ptr, long size)
{
    struct ResourceInformation* event;
    int element = 0;
    long ret = 0;
    uint64_t pidTid = bpf_get_current_pid_tgid();

    //
    // Only trace PIDs that matched the target PID
    //
    uint64_t pid = pidTid >> 32;
    if (pid != target_PID || CheckSampleRate() == false)
    {
        return 1;
    }

    //
    // Get heap element from the map
    //
    event = bpf_map_lookup_elem(&heapStorage, &element);
	if (event == NULL)
    {
		return 1;
    }

    ZeroMemory((char*) event);

    //
    // Setup the event we want to transfer to usermode.
    // The call to bpf_get_stack populates the specified map with the
    // actual call stack.
    //
    event->allocSize = size;
    event->pid = target_PID;
    event->resourceType = REALLOC_ALLOC;
    event->allocAddress = 0;

    //
    // Update the arguments hashmap with the entry. We'll fetch the entry when
    // we exit the realloc and update other fields (such as allocation ptr) and then
    // send to user mode.
    //
    if ((ret = bpf_map_update_elem(&argsHashMap, &pidTid, event, BPF_ANY)) != 0)
    {
        BPF_PRINTK("Failed to update arguments hashmap\n");
        return 1;
    }

    BPF_PRINTK("realloc called with size = %d, target PID = %d, stacklen = %d\n", size, target_PID, event->callStackLen);
	return 0;
}

// ------------------------------------------------------------------------------------------
// uretprobe_remalloc
//
// This is the entry point for the realloc exit uprobe. It's called when realloc is exiting.
// ------------------------------------------------------------------------------------------
SEC("uretprobe/libc.so.6:realloc")
int BPF_KRETPROBE(uretprobe_realloc, void* ret)
{
    uint64_t pidTid = bpf_get_current_pid_tgid();
    struct ResourceInformation* event;

    //
    // Only trace PIDs that matched the target PID
    //
    uint64_t p = pidTid >> 32;
    if (p != target_PID)
    {
        return 1;
    }

    //
    // Get the map storage for event.
    // This was created on the preceding realloc enter probe.
    // If the pid is in our map then we must have stored it
    //
    event = (struct ResourceInformation*) bpf_map_lookup_elem(&argsHashMap, &pidTid);
    if (event == NULL)
    {
        return 1;
    }

    event->allocAddress = (unsigned long) ret;
    event->callStackLen = bpf_get_stack(ctx, event->stackTrace, sizeof(event->stackTrace), BPF_F_USER_STACK) / sizeof(__u64);

    //
    // Send the event to the ring buffer (user land)
    //
	bpf_ringbuf_output(&ringBuffer, event, sizeof(*event), 0);

    //
    // Cleanup args hashmap entry
    //
    bpf_map_delete_elem(&argsHashMap, &pidTid);
    return 0;
}


// ------------------------------------------------------------------------------------------
// uprobe_realloc
//
// This is the entry point for the reallocarray uprobe. It's called when reallocarray is called.
// ------------------------------------------------------------------------------------------
SEC("uprobe/libc.so.6:reallocarray")
int BPF_KPROBE(uprobe_reallocarray, void* ptr, long count, long size)
{
    struct ResourceInformation* event;
    int element = 0;
    long ret = 0;
    uint64_t pidTid = bpf_get_current_pid_tgid();

    //
    // Only trace PIDs that matched the target PID
    //
    uint64_t pid = pidTid >> 32;
    if (pid != target_PID || CheckSampleRate() == false)
    {
        return 1;
    }

    //
    // Get heap element from the map
    //
    event = bpf_map_lookup_elem(&heapStorage, &element);
	if (event == NULL)
    {
		return 1;
    }

    ZeroMemory((char*) event);

    //
    // Setup the event we want to transfer to usermode.
    // The call to bpf_get_stack populates the specified map with the
    // actual call stack.
    //
    event->allocSize = size*count;
    event->pid = target_PID;
    event->resourceType = REALLOCARRAY_ALLOC;
    event->allocAddress = 0;

    //
    // Update the arguments hashmap with the entry. We'll fetch the entry when
    // we exit the reallocarray and update other fields (such as allocation ptr) and then
    // send to user mode.
    //
    if ((ret = bpf_map_update_elem(&argsHashMap, &pidTid, event, BPF_ANY)) != 0)
    {
        BPF_PRINTK("Failed to update arguments hashmap\n");
        return 1;
    }

	return 0;
}

// ------------------------------------------------------------------------------------------
// uretprobe_remalloc
//
// This is the entry point for the reallocarray exit uprobe. It's called when reallocarray is exiting.
// ------------------------------------------------------------------------------------------
SEC("uretprobe/libc.so.6:reallocarray")
int BPF_KRETPROBE(uretprobe_reallocarray, void* ret)
{
    uint64_t pidTid = bpf_get_current_pid_tgid();
    struct ResourceInformation* event;

    //
    // Only trace PIDs that matched the target PID
    //
    uint64_t p = pidTid >> 32;
    if (p != target_PID)
    {
        return 1;
    }

    //
    // Get the map storage for event.
    // This was created on the preceding reallocarray enter probe.
    // If the pid is in our map then we must have stored it
    //
    event = (struct ResourceInformation*) bpf_map_lookup_elem(&argsHashMap, &pidTid);
    if (event == NULL)
    {
        return 1;
    }

    event->allocAddress = (unsigned long) ret;
    event->callStackLen = bpf_get_stack(ctx, event->stackTrace, sizeof(event->stackTrace), BPF_F_USER_STACK) / sizeof(__u64);

    //
    // Send the event to the ring buffer (user land)
    //
	bpf_ringbuf_output(&ringBuffer, event, sizeof(*event), 0);

    //
    // Cleanup args hashmap entry
    //
    bpf_map_delete_elem(&argsHashMap, &pidTid);
    return 0;
}