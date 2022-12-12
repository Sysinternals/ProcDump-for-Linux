ROOT=.
CC=gcc
CFLAGS ?= -Wall
CCFLAGS=$(CFLAGS) -I ./include -pthread -std=gnu99
LIBDIR=lib
OBJDIR=obj
SRCDIR=src
INCDIR=include
BINDIR=bin
TESTDIR=tests/integration
DEPS=$(wildcard $(INCDIR)/*.h)
SRC=$(wildcard $(SRCDIR)/*.c)
TESTSRC=$(wildcard $(TESTDIR)/*.c)
OBJS=$(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRC))
TESTOBJS=$(patsubst $(TESTDIR)/%.c, $(OBJDIR)/%.o, $(TESTSRC))
OUT=$(BINDIR)/procdump
TESTOUT=$(BINDIR)/ProcDumpTestApplication

# Profiler
PROFSRCDIR=profiler/src
PROFINCDIR=profiler/inc
PROFCXXFLAGS ?= -DELPP_NO_DEFAULT_LOG_FILE -DELPP_THREAD_SAFE -g -pthread -shared --no-undefined -Wno-invalid-noreturn -Wno-pragma-pack -Wno-writable-strings -Wno-format-security -fPIC -fms-extensions -DHOST_64BIT -DBIT64 -DPAL_STDCPP_COMPAT -DPLATFORM_UNIX -std=c++11
PROFCLANG=clang++

# Revision value from build pipeline
REVISION:=$(if $(REVISION),$(REVISION),'99999')

# installation directory
DESTDIR ?= /
INSTALLDIR=/usr/bin
MANDIR=/usr/share/man/man1

# package creation directories
BUILDDIR := $(CURDIR)/pkgbuild

# Flags to pass to debbuild/rpmbuild
PKGBUILDFLAGS := --define "_topdir $(BUILDDIR)" -bb

# Command to create the build directory structure
PKGBUILDROOT_CREATE_CMD = mkdir -p $(BUILDDIR)/DEBS $(BUILDDIR)/SDEBS $(BUILDDIR)/RPMS $(BUILDDIR)/SRPMS \
			$(BUILDDIR)/SOURCES $(BUILDDIR)/SPECS $(BUILDDIR)/BUILD $(BUILDDIR)/BUILDROOT

# package details
PKG_VERSION:=$(if $(VERSION),$(VERSION),0.0.0)

all: clean build

build: $(OBJDIR)/ProcDumpProfiler.so $(OBJDIR) $(BINDIR) $(OUT) $(TESTOUT)

install:
	mkdir -p $(DESTDIR)$(INSTALLDIR)
	cp $(BINDIR)/procdump $(DESTDIR)$(INSTALLDIR)
	mkdir -p $(DESTDIR)$(MANDIR)
	cp procdump.1 $(DESTDIR)$(MANDIR)

$(OBJDIR)/ProcDumpProfiler.so: $(PROFSRCDIR)/ClassFactory.cpp $(PROFSRCDIR)/ProcDumpProfiler.cpp $(PROFSRCDIR)/dllmain.cpp $(PROFSRCDIR)/corprof_i.cpp $(PROFSRCDIR)/easylogging++.cc | $(OBJDIR)
	$(PROFCLANG) -o $@ $(PROFCXXFLAGS) -I $(PROFINCDIR) $^
	ld -r -b binary -o $(OBJDIR)/ProcDumpProfiler.o $(OBJDIR)/ProcDumpProfiler.so

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) -c -g -o $@ $< $(CCFLAGS)

$(OBJDIR)/%.o: $(TESTDIR)/%.c | $(OBJDIR)
	$(CC) -c -g -o $@ $< $(CCFLAGS)

$(OUT): $(OBJS) | $(BINDIR)
	$(CC) -o $@ $^ $(OBJDIR)/ProcDumpProfiler.o $(CCFLAGS)

$(TESTOUT): $(TESTOBJS) | $(BINDIR)
	$(CC) -o $@ $^ $(CCFLAGS)

$(OBJDIR): clean
	-mkdir -p $(OBJDIR)

$(BINDIR): clean
	-mkdir -p $(BINDIR)

.PHONY: clean
clean:
	-rm -rf $(OBJDIR)
	-rm -rf $(BINDIR)
	-rm -rf $(BUILDDIR)

test: build
	./tests/integration/run.sh

release: clean tarball

.PHONY: tarball
tarball: clean
	$(PKGBUILDROOT_CREATE_CMD)
	tar --exclude=./pkgbuild --exclude=./.git --transform 's,^\.,procdump-$(PKG_VERSION),' -czf $(BUILDDIR)/SOURCES/procdump-$(PKG_VERSION).tar.gz .
	sed -e "s/@PKG_VERSION@/$(PKG_VERSION)/g" dist/procdump.spec.in > $(BUILDDIR)/SPECS/procdump.spec

.PHONY: deb
deb: tarball
	debbuild --define='_Revision ${REVISION}' $(PKGBUILDFLAGS) $(BUILDDIR)/SPECS/procdump.spec

.PHONY: rpm
rpm: tarball
	rpmbuild --define='_Revision ${REVISION}' $(PKGBUILDFLAGS) $(BUILDDIR)/SPECS/procdump.spec
