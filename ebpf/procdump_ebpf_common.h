/*
    ProcDump for Linux

    Copyright (c) Microsoft Corporation

    All rights reserved.

    MIT License

    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the ""Software""), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef __PROCDUMP_EBPF_COMMON_H__
#define __PROCDUMP_EBPF_COMMON_H__

#define MAX_CALL_STACK_FRAMES   100

#define MALLOC_ALLOC       0x00000001
#define MALLOC_FREE        0x00000002
#define CALLOC_ALLOC       0x00000003
#define REALLOC_ALLOC      0x00000004
#define REALLOCARRAY_ALLOC 0x00000005

struct ResourceInformation
{
    unsigned long allocAddress;
    uint64_t pid;
    unsigned int resourceType;
    unsigned long allocSize;
    long callStackLen;
    __u64 stackTrace[MAX_CALL_STACK_FRAMES];
};

#endif // __PROCDUMP_EBPF_COMMON_H__