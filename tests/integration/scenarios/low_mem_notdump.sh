#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
runProcDumpAndValidate=$(readlink -m "$DIR/../runProcDumpAndValidate.sh");
source $runProcDumpAndValidate

stressPercentage=200M
procDumpType="-m"
procDumpTrigger=80
shouldDump=false

runProcDumpAndValidate $stressPercentage $procDumpType $procDumpTrigger $shouldDump "MEM"
