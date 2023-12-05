#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
runProcDumpAndValidate=$(readlink -m "$DIR/../runProcDumpAndValidate.sh");
source $runProcDumpAndValidate

# TARGETVALUE is only used for stress-ng
TARGETVALUE=90

# These are all the ProcDump switches preceeding the PID
PREFIX="-c 80"

# This are all the ProcDump switches after the PID
POSTFIX=""

# Indicates whether the test should result in a dump or not
SHOULDDUMP=false

# Only applicable to stress-ng and can be either MEM or CPU
RESTYPE="CPU"

# The dump target
DUMPTARGET="missing_subdir/custom_dump_file"

runProcDumpAndValidate