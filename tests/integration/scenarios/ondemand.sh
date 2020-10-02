#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
runProcDumpAndValidate=$(readlink -m "$DIR/../runProcDumpAndValidate.sh");
source $runProcDumpAndValidate

stressPercentage=1
procDumpType=""
procDumpTrigger=""
shouldDump=true

runProcDumpAndValidate "$stressPercentage" "$procDumpType" "$procDumpTrigger" "$shouldDump" "CPU"
