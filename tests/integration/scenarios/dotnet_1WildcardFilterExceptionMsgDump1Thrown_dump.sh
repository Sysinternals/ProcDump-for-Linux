#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
PROCDUMPPATH=$(readlink -m "$DIR/../../../bin/procdump");
TESTWEBAPIPATH=$(readlink -m "$DIR/../TestWebApi");

pushd .
cd $TESTWEBAPIPATH
rm -rf *TestWebApi_*Exception*
dotnet run --urls=http://localhost:5032&
TESTPID=$!
sudo $PROCDUMPPATH -e -f "*current*sta*" -w TestWebApi&
i=0
while ! wget http://localhost:5032/throwinvalidoperation
do
    if [ -f *TestWebApi_*Exception* ]; then
        break
    fi

    ((i=i+1))
    if [[ "$i" -gt 10 ]]; then
        break
    fi

    sleep 5s
done

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