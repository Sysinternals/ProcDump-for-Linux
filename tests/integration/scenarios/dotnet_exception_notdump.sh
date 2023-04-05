#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
PROCDUMPPATH=$(readlink -m "$DIR/../../../bin/procdump");
TESTWEBAPIPATH=$(readlink -m "$DIR/../TestWebApi");

pushd .
cd $TESTWEBAPIPATH
rm -rf *TestWebApi_*Exception*
dotnet run --urls=http://localhost:5032&
TESTPID=$!

#waiting TestWebApi ready to service
i=0
wget http://localhost:5032/throwinvalidoperation
while  [ $? -ne 8 ]
do
    ((i=i+1))
    if [[ "$i" -gt 10 ]]; then
        pkill -9 TestWebApi
        popd
        exit 1
    fi
    sleep 2s
    wget http://localhost:5032/throwinvalidoperation
done

sudo $PROCDUMPPATH -log -e -f System.NonExistingException -w TestWebApi&
PROCDUMPPID=$!

i=0
PROCDUMPCHILDPID=$(ps -o pid= -C "procdump" | tr -d ' ')
while [ ! $PROCDUMPCHILDPID ]
do
    ((i=i+1))
    if [[ "$i" -gt 10 ]]; then
        pkill -9 TestWebApi
        pkill -9 procdump
        popd
        exit 1
    fi
    sleep 1s
    echo waiting for procdump child process started for about $i seconds...
    PROCDUMPCHILDPID=$(ps -o pid= -C "procdump" | tr -d ' ')
done

TESTCHILDPID=$(ps -o pid= --ppid ${TESTPID} | tr -d ' ')

if [[ -v TMPDIR ]];
then
    TMPFOLDER=$TMPDIR
else
    TMPFOLDER="/tmp"
fi
PREFIXNAME="/procdump/procdump-status-"
SOCKETPATH=$TMPFOLDER$PREFIXNAME$PROCDUMPCHILDPID"-"$TESTCHILDPID

#make sure procdump ready to capture before throw exception by checking if socket created
i=0
while  [ ! -S $SOCKETPATH ]
do
    ((i=i+1))
    if [[ "$i" -gt 10 ]]; then
        pkill -9 TestWebApi
        pkill -9 procdump
        popd
        exit 1
    fi
    echo $SOCKETPATH
    sleep 1s
done

wget http://localhost:5032/throwinvalidoperation

sudo pkill -9 procdump
COUNT=( $(ls *TestWebApi_*Exception* | wc -l) )
if [ -S $SOCKETPATH ];
then
    rm $SOCKETPATH
fi

if [[ "$COUNT" -ne 0 ]]; then
    rm -rf *TestWebApi_*Exception*
    sudo pkill -9 TestWebApi
    popd
    exit 1
else
    popd

    #check to make sure profiler so is unloaded
    PROF="$(cat /proc/${TESTCHILDPID}/maps | awk '{print $6}' | grep '\procdumpprofiler.so' | uniq)"
    pkill -9 TestWebApi
    if [[ "$PROF" == "procdumpprofiler.so" ]]; then
        exit 1
    else
        exit 0
    fi
fi