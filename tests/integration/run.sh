#!/bin/bash

echo "You can optionally specify a specific test script to run rather than running all (default)"

failed=0
failedTests="\n"

rootcheck () {
    if [ $(id -u) != "0" ]
    then
        sudo "$0" "$@"
        exit $?
    fi
}

rootcheck

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root"
   exit 1
fi

OS=$(uname -s)
if [ "$OS" != "Darwin" ]; then
    if [ ! -e /usr/bin/stress-ng ]; then
    echo "Please install stress-ng before running this script!"
    exit 1
    fi
fi

if [ ! -e /usr/bin/dotnet ] && [ "$OS" != "Darwin" ]; then
   echo "Please install .NET before running this script!"
   exit 1
fi

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";

function runTest {
        printf "\n========================================================================================\n"
        printf "\nStarting $(basename $1)\n"
	$1 "../../../procdump"

	if [ $? -ne 0 ]; then
		echo "$(basename $1) failed"
                failedTests="$failedTests$(basename $1)\n"
		failed=1
	else
		echo "$(basename $1) passed"
	fi
}


scenarioDir=""
if [ "$OS" = "Darwin" ]; then
    scenarioDir=$DIR/scenarios_mac
else    
    scenarioDir=$DIR/scenarios
fi

echo "Running tests in $scenarioDir"

for file in $scenarioDir/*.sh;
do
    if [ ! -z "$1" ]; then
         if [[ "$file" =~ "$1" ]]; then
            runTest $file
        fi
    else
        runTest $file
    fi
done

printf "\nFailed tests: $failedTests"

if [ "$failed" -eq "1" ]; then
    exit 1
else
    exit 0
fi
