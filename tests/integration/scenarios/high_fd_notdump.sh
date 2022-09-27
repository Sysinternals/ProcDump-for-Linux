#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
runProcDumpAndValidate=$(readlink -m "$DIR/../runProcDumpAndValidate.sh");
source $runProcDumpAndValidate

TESTPROGNAME="ProcDumpTestApplication"
TESTPROGMODE="fc"

stressPercentage=90
procDumpType="-fc"
procDumpTrigger=1000
shouldDump=false

runProcDumpAndValidate $stressPercentage $procDumpType $procDumpTrigger $shouldDump "FILEDESCCOUNT"