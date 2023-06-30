#!/bin/bash
function runProcDumpAndValidate {
	DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
	PROCDUMPPATH=$(readlink -m "$DIR/../../bin/procdump");

	# In cases where the previous scenario is still writing a dump we simply want to kill it
	pkill -9 gdb

	# Make absolutely sure we cleanup dumps from prior run
	rm -rf /tmp/dump_*

	dumpDir=$(mktemp -d -t dump_XXXXXX)
	cd $dumpDir

	dumpParam=""
	if [ "$#" -ge "6" -a -n "$6" ]; then
		dumpParam="$dumpDir/$6"
	fi

	if [ -z "$TESTPROGNAME" ]; then
  	    echo [`date +"%T.%3N"`] Starting stress-ng
		if [ "$5" == "MEM" ]; then
			stress-ng --vm 1 --vm-hang 0 --vm-bytes $1 --timeout 20s -q&
		else
			stress-ng -c 1 -l $1 --timeout 20s -q&
		fi
		pid=$!
		echo "PID: $pid"

	    # Give test app opportunity to start and get into scenario state
		sleep 5s
		echo [`date +"%T.%3N"`] Done waiting for stress-ng to start

		childrenpid=$(pidof -o $pid $(which stress-ng))
			echo "ChildrenPID: $childrenpid"

		childpid=$(echo $childrenpid | cut -d " " -f1)
		echo "ChildPID: $childpid"

		# We launch procdump in background and wait for 10 secs to complete the monitoring
		echo "$PROCDUMPPATH $2 $3 $childpid $dumpParam "
		echo [`date +"%T.%3N"`] Starting ProcDump
		$PROCDUMPPATH -log $2 $3 $childpid $dumpParam&
		pidPD=$!
		echo "ProcDump PID: $pidPD"
		sleep 30s
		echo [`date +"%T.%3N"`] Killing ProcDump
	    if ps -p $pidPD > /dev/null
	    then
		    kill $pidPD
	    fi
	else
		echo [`date +"%T.%3N"`] Starting $TESTPROGNAME
		TESTPROGPATH=$(readlink -m "$DIR/../../bin/$TESTPROGNAME");
		($TESTPROGPATH "$TESTPROGMODE") &
		pid=$!
		echo "Test App: $TESTPROGPATH $TESTPROGMODE"
		echo "PID: $pid"

	    # Give test app opportunity to start and get into scenario state
		sleep 5s
		echo [`date +"%T.%3N"`] Done waiting for $TESTPROGNAME to start

		# We launch procdump in background and wait for 10 secs to complete the monitoring
		echo "$PROCDUMPPATH $2 $3 $dumpParam $TESTPROGNAME"
		echo [`date +"%T.%3N"`] Starting ProcDump
		$PROCDUMPPATH -log $2 $3 $dumpParam "$TESTPROGNAME"&
		pidPD=$!
		echo "ProcDump PID: $pidPD"
		sleep 30s
		echo [`date +"%T.%3N"`] Killing ProcDump
	    if ps -p $pidPD > /dev/null
	    then
		    kill $pidPD
	    fi
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
