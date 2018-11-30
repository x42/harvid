VERSION?=$(shell git describe --tags HEAD || echo "X.X.X")

CFLAGS ?= -Wall -g -O2
PREFIX ?= /usr/local

bindir ?= $(PREFIX)/bin
mandir ?= $(PREFIX)/share/man
libdir ?= $(PREFIX)/lib
docdir ?= $(PREFIX)/share/doc
includedir ?=  $(PREFIX)/include

man1dir ?= $(mandir)/man1
hdocdir ?= $(docdir)/harvid

ECHO=$(shell which echo) -e

ARCHFLAGS=
ARCHINCLUDES=
ARCHLIBES=
LIBEXT=so

ifneq ($(XWIN),)
  CC=$(XWIN)-gcc
  LD=$(XWIN)-ld
  AR=$(XWIN)-ar
  NM=$(XWIN)-nm -B
  RANLIB=$(XWIN)-ranlib
  STRIP=$(XWIN)-strip
  WINPREFIX?=$(HOME)/.wine/drive_c/x-prefix
  WINLIB?=$(WINPREFIX)/lib
  PKG_CONFIG_PATH=$(WINLIB)/pkgconfig/
  ARCHINCLUDES=-I$(WINPREFIX)/include -DHAVE_WINDOWS
  ARCHLIBES=-lwsock32 -lws2_32 -lpthread
  LDFLAGS+=-L$(WINLIB) -L$(WINPREFIX)/bin -mwindows
  UNAME=win32|mingw
  LIBEXT=dll
else
  RANLIB=ranlib
  STRIP=strip
  NM=nm
  UNAME=$(shell uname)
  ifeq ($(UNAME),Darwin)
  ARCHFLAGS+=-headerpad_max_install_names
  LOGODEP=logo.c seek.c
  ECHO=echo
  LIBEXT=dylib
  NM=nm
  else
  ARCHLIBES=-lrt -lpthread
  LIBEXT=so
  NM=nm -B
  endif
endif
