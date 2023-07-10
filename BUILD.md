# Containerized Builds
The Dockerfiles in this repo (located under the `.devcontainer` directory) are the same Dockerfiles that are used on the backend build systems when a PR is built as part of the PR checks. This provides an easy and convenient way to ensure that any changes being made can be built using the same backend infrastructure.

There are two Dockerfiles available:

- `Dockerfile_Ubuntu` (default)
- `Dockerfile_Rocky`

There are two primary ways to build using containers:

1. If you use VS Code the repo has support for VS Code Dev Containers. To use this functionality, you need to have the VS Code Dev Containers extension installed as well as Docker. Once installed open VS Code and the `ProcDump-for-Linux` folder and go to command palette and select "Dev Containers: Rebuild and Reopen in Container".
Once the container has finished building, you will be connected to the newly built container. You can also switch which Dockerfile you are using by setting the `dockerfile` field in the file `devcontainer.json`.
For more information about VS Code Dev Containers please see - https://code.visualstudio.com/docs/devcontainers/containers

2. Use the Dockerfiles located under the `.devcontainer` directory and build/run docker locally.

To build inside the container:
```sh
make
make install
```
# Local Builds
## Prerequisites
- clang v10+
- gcc v10+
- zlib
- make

### Ubuntu
```
sudo apt update
sudo apt -y install gcc make clang gdb zlib1g-dev
```

### Rocky Linux
```
sudo yum install gcc make clang gdb zlib-devel
```

## Build
```sh
make
make install
```

# Building Packages
The distribution packages for Procdump for Linux are constructed utilizing `debbuild` for Debian targets and `rpmbuild` for Fedora targets.

To build a `deb` package of Procdump:
```sh
make && make deb
```

To build a `rpm` package of Procdump:
```sh
make && make rpm
```
