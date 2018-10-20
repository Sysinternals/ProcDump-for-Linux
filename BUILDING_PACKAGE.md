# Building the package

This package is built using [debbuild](https://github.com/ascherer/debbuild) for Debian targets.

The Makefile included in this project helps you to do builds, but you need
to have debbuild installed first.

You can install debbuild from the deb published on [the GitHub project releases](https://github.com/ascherer/debbuild/releases).

Once installed, `make deb` will let you build a Debian package.

For building RPMs, you just need `rpmbuild`. For example, on Fedora:

```bash
$ sudo dnf install rpm-build

```

Once installed, `make rpm` will let you build an RPM package.
