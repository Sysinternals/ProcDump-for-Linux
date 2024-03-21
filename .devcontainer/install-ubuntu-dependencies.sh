#!/bin/bash
echo "APT::Get::Assume-Yes \"true\";" > sudo /etc/apt/apt.conf.d/90assumeyes
DEBIAN_FRONTEND=noninteractive
sudo apt-get update
sudo apt -y install software-properties-common
sudo add-apt-repository "deb http://security.ubuntu.com/ubuntu xenial-security main"
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    jq \
    git \
    iputils-ping \
    libcurl4 \
    libicu55 \
    libunwind8 \
    netcat \
    gdb \
    zlib1g-dev \
    stress-ng \
    wget \
    dpkg-dev \
    fakeroot \
    lsb-release \
    gettext \
    liblocale-gettext-perl \
    pax \
    cmake \
    libelf-dev \
    clang \
    clang-12 \
    llvm \
    build-essential \
    libbpf-dev

# Set preference to clang-12
sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-12 100

# Build and install bpftool
sudo rm -rf /usr/sbin/bpftool
git clone --recurse-submodules https://github.com/libbpf/bpftool.git
cd bpftool/src
make HOSTCC=clang-12
sudo make install
sudo ln -s /usr/local/sbin/bpftool /usr/sbin/bpftool

# install debbuild
wget https://github.com/debbuild/debbuild/releases/download/22.02.1/debbuild_22.02.1-0ubuntu20.04_all.deb \
    && sudo dpkg -i debbuild_22.02.1-0ubuntu20.04_all.deb

# install .NET 6 for signing process and integration tests
wget https://packages.microsoft.com/config/ubuntu/20.04/packages-microsoft-prod.deb -O packages-microsoft-prod.deb
sudo dpkg -i packages-microsoft-prod.deb
rm packages-microsoft-prod.deb
sudo apt -y update && sudo apt-get install -y dotnet-runtime-6.0
sudo apt-get install -y dotnet-sdk-6.0