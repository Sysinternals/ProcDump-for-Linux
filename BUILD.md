# Linux
## Containerized Builds
The Dockerfiles in this repo (located under the `.devcontainer` directory) are the same Dockerfiles that are used on the backend build systems when a PR is built as part of the PR checks. This provides an easy and convenient way to ensure that any changes being made can be built using the same backend infrastructure.

There are three Dockerfiles available:

- `Dockerfile_Ubuntu` (default)
- `Dockerfile_Rocky`
- `Dockerfile_AzureLinux`

There are two primary ways to build using containers:

1. If you use VS Code the repo has support for VS Code Dev Containers. To use this functionality, you need to have the VS Code Dev Containers extension installed as well as Docker. Once installed open VS Code and the `ProcDump-for-Linux` folder and go to command palette and select "Dev Containers: Rebuild and Reopen in Container".
Once the container has finished building, you will be connected to the newly built container. You can also switch which Dockerfile you are using by setting the `dockerfile` field in the file `devcontainer.json`.
For more information about VS Code Dev Containers please see - https://code.visualstudio.com/docs/devcontainers/containers

2. Use the Dockerfiles located under the `.devcontainer` directory and build/run docker locally.

To build inside the container:
```sh
mkdir build
cd build
cmake ..
make
```
## Local Builds
### Prerequisites
#### Ubuntu
```
sudo apt update
sudo apt -y install gcc cmake make clang clang-12 gdb zlib1g-dev libelf-dev build-essential libbpf-dev linux-tools-common linux-tools-$(uname -r)
```

#### Rocky Linux
```
sudo yum install gcc make cmake clang gdb zlib-devel elfutils-libelf-devel libbpf-devel bpftool
```

### Build
```sh
mkdir build
cd build
cmake ..
make
```

## Building Packages
The distribution packages for Procdump for Linux are constructed utilizing `dpkg-deb` for Debian targets and `rpmbuild` for Fedora targets.

Create a deb package:
```sh
make deb
```

Create an rpm package:
```sh
make rpm
```

# macOS
### Prerequisites
Install the clang tool chain. 

### Build
```sh
mkdir build
cd build
cmake ..
make
```

## Building Packages
```sh
make brew
```
