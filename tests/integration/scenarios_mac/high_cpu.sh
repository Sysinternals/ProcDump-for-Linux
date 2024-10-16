#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
runProcDumpAndValidate="$DIR/../runProcDumpAndValidate.sh";
echo "runProcDumpAndValidate: $runProcDumpAndValidate"
source $runProcDumpAndValidate

# TARGETVALUE is only used for stress-ng
TARGETVALUE=90

# These are all the ProcDump switches preceeding the PID
PREFIX="-c 80"

# This are all the ProcDump switches after the PID
POSTFIX=""

# Indicates whether the test should result in a dump or not
SHOULDDUMP=true

# Only applicable to stress-ng and can be either MEM or CPU
RESTYPE="CPU"

# The dump target
DUMPTARGET=""

runProcDumpAndValidate

