#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
runProcDumpAndValidate=$(readlink -m "$DIR/../runProcDumpAndValidate.sh");
source $runProcDumpAndValidate

stressPercentage=40M
procDumpType="-ml"
procDumpTrigger=80
shouldDump=true

runProcDumpAndValidate $stressPercentage $procDumpType $procDumpTrigger $shouldDump "MEM"
