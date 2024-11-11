#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
runProcDumpAndValidate=$DIR/../runProcDumpAndValidate.sh;
source $runProcDumpAndValidate

TESTPROGNAME="ProcDumpTestApplication"
TESTPROGMODE="burn"

# TARGETVALUE is only used for stress-ng
#TARGETVALUE=3M

# These are all the ProcDump switches preceeding the PID
PREFIX="-c 50"

# This are all the ProcDump switches after the PID
POSTFIX=""

# Indicates whether the test should result in a dump or not
SHOULDDUMP=true

# Only applicable to stress-ng and can be either MEM or CPU
RESTYPE=""

# The dump target
DUMPTARGET=""

runProcDumpAndValidate

