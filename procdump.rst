========
procdump
========

-------------------------------------------------------------
Monitor and coredump processes based off performance triggers
-------------------------------------------------------------
:Manual section: 1

SYNOPSIS
========

  procdump [OPTIONS...]

DESCRIPTION
===========

procdump monitors a process CPU and memory usage and dumps the process
core when these values meet certain conditions.

-p PID
  Monitor the process with an id of *PID*.
 
-C CPUTHRESHOLD
  Create a coredump if the process CPU usage goes over *CPUTHRESHOLD*
  percent.

-c CPUTHRESHOLD
  Create a coredump if the process CPU usage goes under *CPUTHRESHOLD*
  percent.
	
-M MEMTHRESHOLD
  Create a coredump if the process memory commit goes over
  *MEMTHRESHOLD* megabytes.

-m MEMTHRESHOLD
  Create a coredump if the process memory commit goes under
  *MEMTHRESHOLD* megabytes.

-n NBDUMP
  Stop monitoring and exit after *NBDUMP* coredumps. This does not kill
  the monitored process.

-s SEC
  Wait at least *SEC* seconds between two coredumps. Default is 10
  seconds.

EXAMPLES
========

Create a core dump immediately::

 sudo procdump -p 1234

Create 3 core dumps 10 seconds apart::

 sudo procdump -n 3 -p 1234

Create 3 core dumps 5 seconds apart::

 sudo procdump -n 3 -s 5 -p 1234

Create a core dump each time the process has CPU usage >= 65%, up to 3
times, with at least 10 seconds between each dump::

 sudo procdump -C 65 -n 3 -p 1234

Create a core dump each time the process has CPU usage >= 65%, up to 3
times, with at least 5 seconds between each dump::

 sudo procdump -C 65 -n 3 -s 5 -p 1234

The following will create a core dump when CPU usage is outside the
range [10,65]::

 sudo procdump -c 10 -C 65 -p 1234

The following will create a core dump when CPU usage is >= 65% or
memory usage is >= 100 MB::

 sudo procdump -C 65 -M 100 -p 1234

BUGS
====

* procdump requires a linux kernel version 3.5 or above.
* procdump does not have full feature parity with Windows version of ProcDump,
  specifically, stay alive functionality, and custom performance
  counters
 
SEE ALSO
========

gdb(1), strace(1), valgrind(1)

AUTHORS
=======

procdump is a Linux reimagining of the class ProcDump tool from the
Sysinternals suite of tools for Windows.

procdump was written by Mark Russinovich, Mario Hewardt, John Salem,
Javid Habibi.

This man page was written by Aurelien Aptel.
