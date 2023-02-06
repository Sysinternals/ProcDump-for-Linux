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
    sleep 3s
    wget http://localhost:5032/throwinvalidoperation
done

sudo $PROCDUMPPATH -log -e -f "current state" -w TestWebApi&
sleep 6s
wget http://localhost:5032/throwinvalidoperation

sudo pkill -9 procdump
COUNT=( $(ls *TestWebApi_*Exception* | wc -l) )

if [[ "$COUNT" -eq 1 ]]; then
    rm -rf *TestWebApi_*Exception*
    popd

    #check to make sure profiler so is unloaded
    PROF="$(cat /proc/${TESTPID}/maps | awk '{print $6}' | grep '\procdumpprofiler.so' | uniq)"
    pkill -9 TestWebApi
    if [[ "$PROF" == "procdumpprofiler.so" ]]; then
        exit 1
    else
        exit 0
    fi
else
    pkill -9 TestWebApi
    popd
    exit 1
fi