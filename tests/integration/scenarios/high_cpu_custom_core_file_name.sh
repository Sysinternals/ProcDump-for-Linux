#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
runProcDumpAndValidate=$(readlink -m "$DIR/../runProcDumpAndValidate.sh");
source $runProcDumpAndValidate

# TARGETVALUE is only used for stress-ng
TARGETVALUE=90

# This are all the ProcDump switches preceeding the PID
PREFIX="-c 80"

# This are all the ProcDump switches after the PID
POSTFIX=""

# Indicates whether the test should result in a dump or not
SHOULDDUMP=true

# The dump target
DUMPTARGET="custom_dump_file"

runProcDumpAndValidate