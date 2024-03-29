#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
runProcDumpAndValidate=$(readlink -m "$DIR/../runProcDumpAndValidate.sh");
source $runProcDumpAndValidate

TESTPROGNAME="ProcDumpTestApplication"
TESTPROGMODE="burn"

# This are all the ProcDump switches preceeding the target
PREFIX="-c 50"

# This are all the ProcDump switches after the target
POSTFIX=""

# Indicates whether the test should result in a dump or not
SHOULDDUMP=true

# Only applicable to stress-ng and can be either MEM or CPU
# RESTYPE="CPU"

# The dump target
DUMPTARGET=""

runProcDumpAndValidate