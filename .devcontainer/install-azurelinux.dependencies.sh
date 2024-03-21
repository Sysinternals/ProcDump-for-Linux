#!/bin/bash

# install all needed packages for builds
yum install -y ca-certificates \
    git \
    gdb \
    zlib-devel \
    gcc \
    rpm-build \
    make \
    curl \
    libcurl-devel \
    libicu-devel \
    libunwind-devel \
    nmap \
    wget \
    clang \
    glibc-devel \
    kernel-headers-5.15.125.1-2.cm2.noarch \
    binutils \
    lsb-release \
    cmake \
    bpftool \
    libbpf-devel \
    sudo \
    which

# install JQ since it doesn't have a .rpm package
curl https://stedolan.github.io/jq/download/linux64/jq > /usr/bin/jq && chmod +x /usr/bin/jq

# install .net core 6 for ESRP signing and integration tests
yum install -y dotnet-sdk-6.0

# Update packages to latest
yum update -y