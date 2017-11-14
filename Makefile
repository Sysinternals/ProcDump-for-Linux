ROOT=.
CC=gcc
CFLAGS=-I./include -pthread
LIBDIR=lib
OBJDIR=obj
SRCDIR=src
INCDIR=include
BINDIR=bin
DEPS=$(wildcard $(INCDIR)/*.h)
SRC=$(wildcard $(SRCDIR)/*.c)
OBJS=$(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRC))
OUT=$(BINDIR)/procdump

all: clean $(OBJDIR) $(BINDIR) $(OUT)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) -c -g -o $@ $< $(CFLAGS)

$(OUT): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

$(OBJDIR):
	-@mkdir -p $(OBJDIR)

$(BINDIR):
	-@mkdir -p $(BINDIR)

clean:
	-rm -r $(OBJDIR)
	-rm -rf $(BINDIR)
