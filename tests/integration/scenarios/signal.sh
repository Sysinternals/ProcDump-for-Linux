#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
runProcDumpAndValidate=$(readlink -m "$DIR/../runProcDumpAndValidate.sh");
source $runProcDumpAndValidate

TESTPROGNAME="ProcDumpTestApplication"
TESTPROGMODE="sig"

stressPercentage=90
procDumpType="-sig"
procDumpTrigger=12
shouldDump=true

runProcDumpAndValidate $stressPercentage "$procDumpType" $procDumpTrigger $shouldDump "FILEDESCCOUNT"