VERSION?=$(shell git describe --tags HEAD || echo "X.X.X")

CFLAGS ?= -Wall -g -O2
PREFIX ?= /usr/local

bindir ?= $(PREFIX)/bin
mandir ?= $(PREFIX)/share/man/man1

LOGODEP=logo.o seek.o
ECHO=$(shell which echo) -e

ARCHFLAGS=
ARCHINCLUDES=
ARCHLIBES=

CONFIGTEMP=conf.out
IS_OSX=

ifeq ($(ARCH),mingw)
  CC=i686-w64-mingw32-gcc
  LD=i686-w64-mingw32-ld
  WINEROOT?=$(HOME)/.wine/drive_c/x-prefix
  PKG_CONFIG_PATH=$(WINEROOT)/lib/pkgconfig/
  ARCHINCLUDES=-I$(WINEROOT)/include -DHAVE_WINDOWS
  ARCHLIBES=-lwsock32 -lws2_32 -lpthreadGC2
  LDFLAGS+=-L$(WINEROOT)/lib/ -L${WINEROOT}/bin
  UNAME=mingw
else
  UNAME=$(shell uname)
  ifeq ($(UNAME),Darwin)
  ARCHFLAGS=-arch i386 -arch ppc -arch x86_64
  ARCHFLAGS+=-isysroot /Developer/SDKs/MacOSX10.5.sdk -mmacosx-version-min=10.5
  ARCHFLAGS+=-headerpad_max_install_names
  ARCHFLAGS+=-sectcreate __DATA __doc_harvid_jpg ../doc/harvid.jpg
  ARCHFLAGS+=-sectcreate __DATA __doc_seek_js ../doc/seek.js
  LOGODEP=
  ECHO=echo
  ISOSX=1
  else
  ARCHLIBES=-lrt -lpthread
  endif
endif


ifeq ($(shell pkg-config --exists libavcodec libavformat libswscale || echo no), no)
  $(error "http://ffmpeg.org is required - install libavcodec-dev, libswscale-dev, etc")
endif

ifeq ($(shell pkg-config --exists libpng || echo no), no)
  $(error "libpng is required - install libpng-dev")
endif

ifeq ($(shell $(ECHO) "\#include <stdio.h>\n\#include <jpeglib.h>\nint main() { struct jpeg_error_mgr jerr; jpeg_std_error(&jerr); return 0; }" | $(CC) -pipe -x c -o $(CONFIGTEMP) $(ARCHINCLUDES) $(LDFLAGS) - -ljpeg 2>/dev/null || echo no; $(RM) -f $(CONFIGTEMP)), no)
  $(error "libjpeg is required - install libjpeg-dev, libjpeg8-dev or libjpeg62-dev")
endif


FLAGS=-I../libharvid/
FLAGS+=$(ARCHINCLUDES) $(ARCHFLAGS)
FLAGS+=`pkg-config --cflags libavcodec libavformat libavutil libpng libswscale`

LOADLIBES=$(ARCHLIBES)
LOADLIBES+=`pkg-config --libs libavcodec libavformat libavutil libpng libswscale`
LOADLIBES+=-ljpeg
LOADLIBES+=-lz -lm

FLAGS+=-DICSVERSION="\"$(VERSION)\"" -DICSARCH="\"$(UNAME)\""

all: harvid

HARVID_H = \
  daemon_log.h daemon_util.h \
  socket_server.h \
  enums.h \
  favicon.h \
  ics_handler.h httprotocol.h htmlconst.h \
  image_format.h \
  ../libharvid/vinfo.h \
  ../libharvid/frame_cache.h \
  ../libharvid/image_cache.h\
  ../libharvid/ffdecoder.h \
  ../libharvid/decoder_ctrl.h \
  ../libharvid/ffcompat.h \
  ../libharvid/timecode.h

HARVID_SRC = \
  harvid.c \
  daemon_log.c daemon_util.c \
  fileindex.c htmlseek.c \
  httprotocol.c ics_handler.c \
  image_format.c \
  socket_server.c \
  ../libharvid/vinfo.c \
  ../libharvid/decoder_ctrl.c \
  ../libharvid/frame_cache.c \
  ../libharvid/image_cache.c \
  ../libharvid/ffdecoder.c \
  ../libharvid/timecode.c

logo.o: ../doc/harvid.jpg
	$(LD) -r -b binary -o logo.o ../doc/harvid.jpg

seek.o: ../doc/seek.js
	$(LD) -r -b binary -o seek.o ../doc/seek.js

harvid: $(HARVID_SRC) $(HARVID_H) $(LOGODEP)
	export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH);\
	$(CC) -o $(@) $(CFLAGS) $(FLAGS) $(HARVID_SRC) $(LOGODEP) $(LDFLAGS) $(LOADLIBES)

clean:
	rm -f harvid logo.o seek.o cscope.* tags

man: harvid
	help2man -N -n 'video server' -o ../doc/harvid.1 ./harvid

install: install-bin

uninstall: uninstall-bin

install-bin: harvid
	install -d $(DESTDIR)$(bindir)
	install -m755 harvid $(DESTDIR)$(bindir)

uninstall-bin:
	rm -f $(DESTDIR)$(bindir)/harvid
	-rmdir $(DESTDIR)$(bindir)

install-man:

uninstall-man:

.PHONY: all install uninstall install-man uninstall-man install-bin uninstall-bin
