#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";
PROCDUMPPATH=$(readlink -m "$DIR/../../../bin/procdump");

pushd .
cd TestWebApi
dotnet run --urls=http://localhost:5032&
sleep 5s
sudo $PROCDUMPPATH -e -f System.InvalidOperationException TestWebApi testdump&
sleep 5s
wget http://localhost:5032/throwinvalidoperation
sleep 5s
pkill -9 TestWebApi
if [ -f "testdump_0" ]; then
    rm -rf testdump_0
    popd
    exit 0
else
    popd
    exit 1
fi


