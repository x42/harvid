VERSION?=$(shell git describe --tags HEAD || echo "X.X.X")

CFLAGS ?= -Wall -g -O2
PREFIX ?= /usr/local

bindir ?= $(PREFIX)/bin
mandir ?= $(PREFIX)/share/man/man1

ECHO=$(shell which echo) -e

ARCHFLAGS=
ARCHINCLUDES=
ARCHLIBES=

ifeq ($(ARCH),mingw)
  CC=i686-w64-mingw32-gcc
  LD=i686-w64-mingw32-ld
  AR=i686-w64-mingw32-ar
  RANLIB=i686-w64-mingw32-ranlib
  WINEROOT?=$(HOME)/.wine/drive_c/x-prefix
  PKG_CONFIG_PATH=$(WINEROOT)/lib/pkgconfig/
  ARCHINCLUDES=-I$(WINEROOT)/include -DHAVE_WINDOWS
  ARCHLIBES=-lwsock32 -lws2_32 -lpthreadGC2
  LDFLAGS+=-L$(WINEROOT)/lib/ -L${WINEROOT}/bin
  UNAME=mingw
else
  RANLIB=ranlib
  UNAME=$(shell uname)
  ifeq ($(UNAME),Darwin)
  ARCHFLAGS=-arch i386 -arch ppc -arch x86_64
  ARCHFLAGS+=-isysroot /Developer/SDKs/MacOSX10.5.sdk -mmacosx-version-min=10.5
  ARCHFLAGS+=-headerpad_max_install_names
  ARCHFLAGS+=-sectcreate __DATA __doc_harvid_jpg ../doc/harvid.jpg
  ARCHFLAGS+=-sectcreate __DATA __doc_seek_js ../doc/seek.js
  LOGODEP=
  ECHO=echo
  else
  ARCHLIBES=-lrt -lpthread
  endif
endif
