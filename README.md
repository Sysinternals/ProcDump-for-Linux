# ProcDump [![Build Status](https://dev.azure.com/sysinternals/Tools/_apis/build/status/Sysinternals.ProcDump-for-Linux?branchName=master)](https://dev.azure.com/sysinternals/Tools/_build/latest?definitionId=341&branchName=master)
ProcDump is a Linux reimagining of the classic ProcDump tool from the Sysinternals suite of tools for Windows.  ProcDump provides a convenient way for Linux developers to create core dumps of their application based on performance triggers. ProcDump for Linux is part of [Sysinternals](https://sysinternals.com).

![ProcDump in use](procdump.gif "Procdump in use")

# Installation & Usage

## Requirements
* Minimum OS:
  * Red Hat Enterprise Linux / CentOS 7
  * Fedora 29
  * Ubuntu 16.04 LTS
* `gdb` >= 7.6.1

## Install ProcDump
Please see installation instructions [here](INSTALL.md).

## Build
Please see build instructions [here](BUILD.md).

## Usage
**BREAKING CHANGE** With the release of ProcDump 1.3 the switches are now aligned with the Windows ProcDump version.
```
procdump [-n Count]
        [-s Seconds]
        [-c|-cl CPU_Usage]
        [-m|-ml Commit_Usage1[,Commit_Usage2,...]]
        [-gcm [<GCGeneration>: | LOH: | POH:]Memory_Usage1[,Memory_Usage2...]]
        [-gcgen Generation]
        [-tc Thread_Threshold]
        [-fc FileDescriptor_Threshold]
        [-sig Signal_Number]
        [-e]
        [-f Include_Filter,...]
        [-pf Polling_Frequency]
        [-o]
        [-log]
        {
          {{[-w] Process_Name | [-pgid] PID} [Dump_File | Dump_Folder]}
        }

Options:
   -n      Number of dumps to write before exiting.
   -s      Consecutive seconds before dump is written (default is 10).
   -c      CPU threshold above which to create a dump of the process.
   -cl     CPU threshold below which to create a dump of the process.
   -m      Memory commit threshold(s) (MB) above which to create dumps.
   -ml     Memory commit threshold(s) (MB) below which to create dumps.
   -gcm    [.NET] GC memory threshold(s) (MB) above which to create dumps for the specified generation or heap (default is total .NET memory usage).
   -gcgen  [.NET] Create dump when the garbage collection of the specified generation starts and finishes.
   -tc     Thread count threshold above which to create a dump of the process.
   -fc     File descriptor count threshold above which to create a dump of the process.
   -sig    Signal number to intercept to create a dump of the process.
   -e      [.NET] Create dump when the process encounters an exception.
   -f      [.NET] Filter (include) on the (comma seperated) exception name(s) and exception message(s). Supports wildcards.
   -pf     Polling frequency.
   -o      Overwrite existing dump file.
   -log    Writes extended ProcDump tracing to syslog.
   -w      Wait for the specified process to launch if it's not running.
   -pgid   Process ID specified refers to a process group ID.
```
### Examples
> The following examples all target a process with pid == 1234

The following will create a core dump immediately.
```
sudo procdump 1234
```
The following will create 3 core dumps 10 seconds apart.
```
sudo procdump -n 3 1234
```
The following will create 3 core dumps 5 seconds apart.
```
sudo procdump -n 3 -s 5 1234
```
The following will create a core dump each time the process has CPU usage >= 65%, up to 3 times, with at least 10 seconds between each dump.
```
sudo procdump -c 65 -n 3 1234
```
The following will create a core dump each time the process has CPU usage >= 65%, up to 3 times, with at least 5 seconds between each dump.
```
sudo procdump -c 65 -n 3 -s 5 1234
```
The following will create a core dump when CPU usage is outside the range [10,65].
```
sudo procdump -cl 10 -c 65 1234
```
The following will create a core dump when CPU usage is >= 65% or memory usage is >= 100 MB.
```
sudo procdump -c 65 -m 100 1234
```
The following will create a core dump when memory usage is >= 100 MB followed by another dump when memory usage is >= 200MB.
```
sudo procdump -m 100,200 1234
```
The following will create a core dump when the total .NET memory usage is >= 100 MB followed by another dump when memory usage is >= 200MB.
```
sudo procdump -gcm 100,200 1234
```
The following will create a core dump when .NET memory usage for generation 1 is >= 1 MB followed by another dump when memory usage is >= 2MB.
```
sudo procdump -gcm 1:1,2 1234
```
The following will create a core dump when .NET Large Object Heap memory usage is >= 100 MB followed by another dump when memory usage is >= 200MB.
```
sudo procdump -gcm LOH:100,200 1234
```
The following will create a core dump at the start and end of a .NET generation 1 garbage collection.
```
sudo procdump -gcgen 1
```
The following will create a core dump in the `/tmp` directory immediately.
```
sudo procdump 1234 /tmp
```
The following will create a core dump in the current directory with the name dump_0.1234. If -n is used, the files will be named dump_0.1234, dump_1.1234 and so on.
```
sudo procdump 1234 dump
```
The following will create a core dump when a SIGSEGV occurs.
```
sudo procdump -sig 11 1234
```
The following will create a core dump when the target .NET application throws a System.InvalidOperationException
```
sudo procdump -e -f System.InvalidOperationException 1234
```
The include filter supports partial and wildcard matching, so the following will create a core dump too for a System.InvalidOperationException
```
sudo procdump -e -f InvalidOperation 1234
```
or
```
sudo procdump -e -f "*Invali*Operation*" 1234
```
> All options can also be used with `-w`, to wait for any process with the given name.

The following waits for a process named `my_application` and creates a core dump immediately when it is found.
```
sudo procdump -w my_application
```

## Current Limitations
* Currently will only run on Linux Kernels version 3.5+
* Does not have full feature parity with Windows version of ProcDump, specifically, stay alive functionality, and custom performance counters

# Feedback
* Ask a question on StackOverflow (tag with ProcDumpForLinux)
* Request a new feature on GitHub
* Vote for popular feature requests
* File a bug in GitHub Issues

# Contributing
If you are interested in fixing issues and contributing directly to the code base, please see the [document How to Contribute](CONTRIBUTING.md), which covers the following:
* How to build and run from source
* The development workflow, including debugging and running tests
* Coding Guidelines
* Submitting pull requests

Please see also our [Code of Conduct](CODE_OF_CONDUCT.md).


# License
Copyright (c) Microsoft Corporation. All rights reserved.

Licensed under the MIT License.