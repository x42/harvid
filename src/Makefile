VERSION=$(shell git describe --tags HEAD || echo "X.X.X")

CFLAGS ?= -Wall -g -O2
PREFIX ?= /usr/local

bindir ?= $(PREFIX)/bin
mandir ?= $(PREFIX)/share/man/man1


ifeq ($(shell pkg-config --exists libavcodec libavformat libswscale || echo no), no)
  $(error "http://ffmpeg.org is required - install libavcode-dev, libswcale-dev, etc")
endif

ifeq ($(shell pkg-config --exists libpng || echo no), no)
  $(error "libpng is required - install libpng-dev")
endif

ifeq ($(shell echo -e '\#include <stdio.h>\n\#include <jpeglib.h>\nint main() { struct jpeg_error_mgr jerr; jpeg_std_error(&jerr); return 0; }' | gcc -x c -o /dev/null - -ljpeg &>/dev/null || echo no), no)
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

harvid: $(HARVID_SRC) $(HARVID_H)
	export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH);\
	$(CC) -o $(@) $(CFLAGS) $(HARVID_SRC) $(LDFLAGS) $(LOADLIBES)

clean:
	rm -f harvid cscope.* tags

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
