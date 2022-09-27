#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
runProcDumpAndValidate=$(readlink -m "$DIR/../runProcDumpAndValidate.sh");
TESTPROGNAME="ProcDumpTestApplication"
TESTPROGMODE="sleep"
source $runProcDumpAndValidate

stressPercentage=10
procDumpType="-cl"
procDumpTrigger=20
shouldDump=true

runProcDumpAndValidate $stressPercentage $procDumpType $procDumpTrigger $shouldDump "CPU"
