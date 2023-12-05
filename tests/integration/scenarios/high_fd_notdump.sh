#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
runProcDumpAndValidate=$(readlink -m "$DIR/../runProcDumpAndValidate.sh");
source $runProcDumpAndValidate

TESTPROGNAME="ProcDumpTestApplication"
TESTPROGMODE="fc"

# TARGETVALUE is only used for stress-ng
#TARGETVALUE=3M

# These are all the ProcDump switches preceeding the PID
PREFIX="-fc 1000"

# This are all the ProcDump switches after the PID
POSTFIX=""

# Indicates whether the test should result in a dump or not
SHOULDDUMP=false

# Only applicable to stress-ng and can be either MEM or CPU
RESTYPE=""

# The dump target
DUMPTARGET=""

runProcDumpAndValidate