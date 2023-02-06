#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
PROCDUMPPATH=$(readlink -m "$DIR/../../../bin/procdump");
TESTWEBAPIPATH=$(readlink -m "$DIR/../TestWebApi");

pushd .
cd $TESTWEBAPIPATH
rm -rf *TestWebApi_*Exception*
dotnet run --urls=http://localhost:5032&
TESTPID=$!
sleep 10s
sudo $PROCDUMPPATH -log -e TestWebApi&
PDPID=$!
sudo kill -2 ${PDPID}
popd
sleep 5s

#check to make sure profiler so is unloaded
PROF="$(cat /proc/${TESTPID}/maps | awk '{print $6}' | grep '\procdumpprofiler.so' | uniq)"
pkill -9 TestWebApi
if [[ "$PROF" == "procdumpprofiler.so" ]]; then
    exit 1
else
    exit 0
fi