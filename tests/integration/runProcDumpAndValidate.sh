#!/bin/bash
function runProcDumpAndValidate {
	DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
	PROCDUMPPATH=$(readlink -m "$DIR/../../bin/procdump");

	dumpDir=$(mktemp -d -t dump_XXXXXX)
	cd $dumpDir

	dumpParam=""
	if [ "$#" -ge "6" -a -n "$6" ]; then
		dumpParam="$dumpDir/$6"
	fi

	if [ -z "$TESTPROGNAME" ]; then
		if [ "$5" == "MEM" ]; then
			stress-ng --vm 1 --vm-hang 0 --vm-bytes $1 --timeout 20s -q&
		else
			stress-ng -c 1 -l $1 --timeout 20s -q&
		fi
		pid=$!
		echo "PID: $pid"

		sleep 1s

		childrenpid=$(pidof -o $pid $(which stress-ng))
			echo "ChildrenPID: $childrenpid"

		childpid=$(echo $childrenpid | cut -d " " -f1)
		echo "ChildPID: $childpid"

		echo "$PROCDUMPPATH $2 $3 $childpid $dumpParam "
		$PROCDUMPPATH $2 $3 $childpid $dumpParam
	else
		TESTPROGPATH=$(readlink -m "$DIR/../../bin/$TESTPROGNAME");
		($TESTPROGPATH "$TESTPROGMODE") &
		pid=$!
		echo "Test App: $TESTPROGPATH $TESTPROGMODE"
		echo "PID: $pid"

		echo "$PROCDUMPPATH $2 $3 $dumpParam $TESTPROGNAME"
		$PROCDUMPPATH $2 $3 $dumpParam "$TESTPROGNAME"
	fi

	if ps -p $pid > /dev/null
	then
		kill $pid
	fi

	if find "$dumpDir" -mindepth 1 -print -quit | grep -q .; then
		if $4; then
			exit 0
		else
			exit 1
		fi
	else
		if $4; then
			exit 1
		else
			exit 0
		fi
	fi
}
