#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
PROCDUMPPATH=$(readlink -m "$DIR/../../../bin/procdump");
TESTWEBAPIPATH=$(readlink -m "$DIR/../TestWebApi");

pushd .
cd $TESTWEBAPIPATH
rm -rf *TestWebApi_*Exception*
dotnet run --urls=http://localhost:5032&
TESTPID=$!
sudo $PROCDUMPPATH -e -f System.NonExistingException -w TestWebApi&

sleep 30s
wget http://localhost:5032/throwinvalidoperation

sudo pkill -9 procdump
COUNT=( $(ls *TestWebApi_*Exception* | wc -l) )

if [[ "$COUNT" -ne 0 ]]; then
    rm -rf *TestWebApi_*Exception*
    sudo pkill -9 TestWebApi
    popd
    exit 1
else
    popd

    #check to make sure profiler so is unloaded
    PROF="$(cat /proc/${TESTPID}/maps | awk '{print $6}' | grep '\procdumpprofiler.so' | uniq)"
    pkill -9 TestWebApi
    if [[ "$PROF" == "procdumpprofiler.so" ]]; then
        exit 1
    else
        exit 0
    fi
fi