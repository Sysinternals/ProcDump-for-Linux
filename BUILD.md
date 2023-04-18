# Build

## Prerequisites
- clang/llvm v10+
- gcc v10+
- zlib
- make

## Build
```sh
make
make install
```

## Building Packages
The distribution packages for Procdump for Linux are constructed utilizing `debbuild` for Debian targets and `rpmbuild` for Fedora targets.

To build a `deb` package of Procdump:
```sh
make && make deb
```

To build a `rpm` package of Procdump:
```sh
make && make rpm
```
