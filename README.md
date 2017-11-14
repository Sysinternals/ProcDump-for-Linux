# ProcDump
ProcDump is a Linux reimagining of the classic ProcDump tool from the Sysinternals suite of tools for Windows.  ProcDump provides a convenient way for Linux developers to create core dumps of their application based on performance triggers.

![ProcDump in use](procdump.gif "Procdump in use")

# Installation & Usage 

## Requirements
* Minimum OS: Ubuntu 14.04 LTS (Desktop or Server)
  * We are actively testing against other Linux distributions.  If you have requests for specific distros, please let us know (or create a pull request with the necessary changes).
* `gdb` (>=7.7.1)

## Install ProcDump
### Via Package Manager [prefered method]
#### Linux (Ubuntu 14.04+)
```sh

sudo sh -c 'echo "deb [arch=amd64] <URI of distribution endpoint> external main" > /etc/apt/sources.list.d/oss-sysinternals.list'
sudo apt-get update
sudo apt-get install procdump
```
### Via `.deb` Package
#### Linux (Ubuntu 14.04+)

> Pre-Depends: `dpkg`(>=1.17.5) 

```sh
wget <URI of distribution endpoint>
sudo dpkg -i procdump_1.0_amd64.deb
sudo apt-get -f install
```
### Uninstall
#### Linux (Ubuntu 14.04+)
```sh
sudo apt-get purge procdump
```
## Usage
```
Usage: procdump [OPTIONS...] TARGET
   OPTIONS
      -C          CPU threshold at which to create a dump of the process from 0 to 200
      -c          CPU threshold below which to create a dump of the process from 0 to 200
      -M          Memory commit threshold in MB at which to create a dump
      -m          Trigger when memory commit drops below specified MB value.
      -n          Number of dumps to write before exiting
      -s          Consecutive seconds before dump is written (default is 10)
   TARGET must be exactly one of these:
      -p          pid of the process
```
### Examples
> The following examples all target a process with pid == 1234

The following will create a core dump immediately.
```
sudo procdump -p 1234
```
The following will create 3 core dumps 10 seconds apart.
```
sudo procdump -n 3 -p 1234
```
The following will create 3 core dumps 5 seconds apart.
```
sudo procdump -n -s 5 -p 1234
```
The following will create a core dump each time the process has CPU usage >= 65%, up to 3 times, with at least 10 seconds between each dump.
```
sudo procdump -C 65 -n 3 -p 1234
```
The following with create a core dump each time the process has CPU usage >= 65%, up to 3 times, with at least 5 seconds between each dump.
```
sudo procdump -C 65 -n 3 -s 5 -p 1234
```
The following will create a core dump when CPU usage is outside the range [10,65].
```
sudo procdump -c 10 -C 65 -p 1234
```
The following will create a core dump when CPU usage is >= 65% or memory usage is >= 100 MB.
```
sudo procdump -C 65 -M 100 -p 1234
```
## Current Limitations
* Has only been tested on Ubuntu 14.04+
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

