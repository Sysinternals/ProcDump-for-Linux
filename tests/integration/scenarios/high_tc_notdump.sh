#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
runProcDumpAndValidate=$(readlink -m "$DIR/../runProcDumpAndValidate.sh");
source $runProcDumpAndValidate

TESTPROGNAME="ProcDumpTestApplication"
TESTPROGMODE="tc"

stressPercentage=90
procDumpType="-tc"
procDumpTrigger=500
shouldDump=false

runProcDumpAndValidate $stressPercentage $procDumpType $procDumpTrigger $shouldDump "THREADCOUNT"