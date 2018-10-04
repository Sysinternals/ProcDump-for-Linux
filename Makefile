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


# installation directory
DESTDIR ?= /
INSTALLDIR=/usr/bin
MANDIR=/usr/share/man/man1

# package creation directories
RELEASEDIR=release
RELEASEBINDIR=$(RELEASEDIR)/procdump/usr/bin
RELEASECONTROLDIR=$(RELEASEDIR)/procdump/DEBIAN
RELEASEMANDIR=$(RELEASEDIR)/procdump/usr/share/man/man1

# package details
PKG_VERSION=1.0.1
PKG_ARCH=amd64
PKG_DEB=procdump_$(PKG_VERSION)_$(PKG_ARCH).deb

all: clean build

build: $(OBJDIR) $(BINDIR) $(OUT) $(TESTOUT)

install:
	mkdir -p $(DESTDIR)$(INSTALLDIR)
	cp $(BINDIR)/procdump $(DESTDIR)$(INSTALLDIR)
	mkdir -p $(DESTDIR)$(MANDIR)
	cp procdump.1 $(DESTDIR)$(MANDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) -c -g -o $@ $< $(CCFLAGS)

$(OBJDIR)/%.o: $(TESTDIR)/%.c
	$(CC) -c -g -o $@ $< $(CCFLAGS)

$(OUT): $(OBJS)
	$(CC) -o $@ $^ $(CCFLAGS)

$(TESTOUT): $(TESTOBJS)
	$(CC) -o $@ $^ $(CCFLAGS)

$(OBJDIR):
	-@mkdir -p $(OBJDIR)

$(BINDIR):
	-@mkdir -p $(BINDIR)

clean:
	-rm -rf $(OBJDIR)
	-rm -rf $(BINDIR)
	-rm -rf $(RELEASEDIR)

test: build
	./tests/integration/run.sh

release: deb tarball

deb: build
	mkdir -p $(RELEASEBINDIR)
	mkdir -p $(RELEASECONTROLDIR)
	mkdir -p $(RELEASEMANDIR)
	md5sum $(OUT) > $(RELEASECONTROLDIR)/md5sums
	cp $(OUT) $(RELEASEBINDIR)
	cp DEBIAN_PACKAGE.control $(RELEASECONTROLDIR)/control
	cp procdump.1 $(RELEASEMANDIR)
	dpkg-deb -b $(RELEASEDIR)/procdump $(RELEASEDIR)/$(PKG_DEB)
	rm -rf $(RELEASEDIR)/procdump

tarball:
	mkdir -p $(RELEASEDIR)
	tar -czf $(RELEASEDIR)/procdump_$(PKG_VERSION).tar.gz Makefile README.md CODE_OF_CONDUCT.md CONTRIBUTING.md DEBIAN_PACKAGE.control procdump.1 ./tests ./include ./src

