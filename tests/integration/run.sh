#!/bin/bash

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

if ! command -v stress-ng &> /dev/null
then
  bold=""
  normal=""
  if command -v tput &> /dev/null
  then
    bold="$(tput bold)"
    normal="$(tput sgr0)"
  fi
  (command -v apt-get &> /dev/null) || { echo -e "Please install ${bold}stress-ng${normal} using your distributions package manager (or build from source)"; exit 1; }
  (apt-get update -qq && apt-get install stress-ng -qq) || exit 1
fi
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";

function runTest {
        printf "\nStarting $(basename $1)\n"
	$1

	if [ $? -ne 0 ]; then
		echo "$(basename $1) failed"
                failedTests="$failedTests$(basename $1)\n"
		failed=1
	else
		echo "$(basename $1) passed"
	fi
}

for file in $DIR/scenarios/*.sh
do
  runTest $file
done

printf "\nFailed tests: $failedTests"

if [ "$failed" -eq "1" ]; then
    exit 1
else
    exit 0
fi
