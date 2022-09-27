#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
runProcDumpAndValidate=$(readlink -m "$DIR/../runProcDumpAndValidate.sh");
source $runProcDumpAndValidate

stressPercentage=90
procDumpType="-c"
procDumpTrigger=80
shouldDump=true
customDumpFileName="custom_dump_file"

runProcDumpAndValidate $stressPercentage $procDumpType $procDumpTrigger $shouldDump "CPU" $customDumpFileName
