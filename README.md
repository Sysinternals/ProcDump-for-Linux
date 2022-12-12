# ProcDump [![Build Status](https://dev.azure.com/sysinternals/Tools/_apis/build/status/Sysinternals.ProcDump-for-Linux?branchName=master)](https://dev.azure.com/sysinternals/Tools/_build/latest?definitionId=341&branchName=master)
ProcDump is a Linux reimagining of the classic ProcDump tool from the Sysinternals suite of tools for Windows.  ProcDump provides a convenient way for Linux developers to create core dumps of their application based on performance triggers.

![ProcDump in use](procdump.gif "Procdump in use")

# Installation & Usage

## Requirements
* Minimum OS:
  * Red Hat Enterprise Linux / CentOS 7
  * Fedora 29
  * Ubuntu 16.04 LTS
* `gdb` >= 7.6.1
* `zlib` (build-time only)
* `clang`

## Install ProcDump
Checkout our [install instructions](INSTALL.md) for distribution specific steps to install Procdump.

## Build ProcDump from Scratch
To build from scratch you'll need to have a C compiler (supporting C11), `zlib`, and a `make` utility installed. Then simply run:

```
make
make install
```

### Building Procdump Packages
The distribution packages for Procdump for Linux are constructed utilizing `debbuild` for Debian targets and `rpmbuild` for Fedora targets.

To build a `deb` package of Procdump on Ubuntu simply run:
```sh
make && make deb
```

To build a `rpm` package of Procdump on Fedora simply run:
```sh
make && make rpm
```

## Usage
**BREAKING CHANGE** With the release of ProcDump 1.3 the switches are now aligned with the Windows ProcDump version.
```
procdump [-n Count]
         [-s Seconds]
         [-c|-cl CPU_Usage]
         [-m|-ml Commit_Usage]
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
   -m      Memory commit threshold in MB at which to create a dump.
   -ml     Trigger when memory commit drops below specified MB value.
   -tc     Thread count threshold above which to create a dump of the process.
   -fc     File descriptor count threshold above which to create a dump of the process.
   -sig    Signal number to intercept to create a dump of the process.
   -e      [.NET] Create dump when the process encounters an exception.
   -f      [.NET] Filter (include) on the (comma seperated) exception name(s).
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

