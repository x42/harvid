VERSION=$(shell git describe --tags HEAD || echo "X.X.X")

CFLAGS ?= -Wall -g -O2
PREFIX ?= /usr/local

bindir ?= $(PREFIX)/bin
mandir ?= $(PREFIX)/share/man/man1

LOGODEP=logo.o
ECHOARG=-e

ifeq ($(ARCH),mingw)
  CC=i686-w64-mingw32-gcc
  LD=i686-w64-mingw32-ld
  WINEROOT?=$(HOME)/.wine/drive_c/x-prefix
  PKG_CONFIG_PATH=$(WINEROOT)/lib/pkgconfig/
  ARCHFLAGS=-I$(WINEROOT)/include -DHAVE_WINDOWS
  ARCHLIBES=-lwsock32 -lws2_32 -lpthreadGC2
  LDFLAGS=-L$(WINEROOT)/lib/ -L${WINEROOT}/bin
else
  ifeq ($(shell uname),Darwin)
  ARCHFLAGS=-arch i386 -arch ppc -arch x86_64
  ARCHFLAGS+=-isysroot /Developer/SDKs/MacOSX10.5.sdk -mmacosx-version-min=10.5
  ARCHFLAGS+=-headerpad_max_install_names -fnested-functions
  ARCHFLAGS+=-sectcreate __DATA __harvid_jpg ../doc/harvid.jpg
	LOGODEP=
	ECHOARG=
  else
  ARCHLIBES=-lrt -lpthread
  endif
endif


ifeq ($(shell pkg-config --exists libavcodec libavformat libswscale || echo no), no)
  $(error "http://ffmpeg.org is required - install libavcode-dev, libswcale-dev, etc")
endif

ifeq ($(shell pkg-config --exists libpng || echo no), no)
  $(error "libpng is required - install libpng-dev")
endif

ifeq ($(shell echo $(ECHOARG) "\#include <stdio.h>\n\#include <jpeglib.h>\nint main() { struct jpeg_error_mgr jerr; jpeg_std_error(&jerr); return 0; }" | gcc -pipe -x c -o /dev/null - -ljpeg  || echo no), no)
  $(error "libjpeg is required - install libjpeg-dev")
endif


CFLAGS+=$(ARCHFLAGS)
CFLAGS+=`pkg-config --cflags libavcodec libavformat libavutil libpng libswscale`

LOADLIBES=$(ARCHLIBES)
LOADLIBES+=`pkg-config --libs libavcodec libavformat libavutil libpng libswscale`
LOADLIBES+=-ljpeg
LOADLIBES+=-lz -lm

CFLAGS+=-DICSD_RGB24
CFLAGS+=-DICSVERSION=\"$(VERSION)\"

all: harvid

HARVID_H = \
  daemon_log.h daemon_util.h \
  socket_server.h \
  vinfo.h frame_cache.h ffdecoder.h \
  decoder_ctrl.h \
  ffcompat.h \
  enums.h \
  favicon.h \
  ics_handler.h httprotocol.h \
  image_format.h \
  timecode.h

HARVID_SRC = \
  harvid.c \
  daemon_log.c daemon_util.c \
  fileindex.c httprotocol.c ics_handler.c \
  socket_server.c vinfo.c \
  decoder_ctrl.c frame_cache.c ffdecoder.c \
  image_format.c \
  timecode.c

logo.o: ../doc/harvid.jpg
	$(LD) -r -b binary -o logo.o ../doc/harvid.jpg

harvid: $(HARVID_SRC) $(HARVID_H) $(LOGODEP)
	export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH);\
	$(CC) -o $(@) $(CFLAGS) $(HARVID_SRC) $(LOGODEP) $(LDFLAGS) $(LOADLIBES)

clean:
	rm -f harvid logo.o cscope.* tags

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
