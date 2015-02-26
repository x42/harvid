ifneq ($(shell which xxd),)
  LOGODEP=logo.c seek.c
else
  LOGODEP=logo.o seek.o
endif

include ../common.mak

CONFIGTEMP=conf.out

ifeq ($(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --exists libavcodec libavformat libavutil libswscale || echo no), no)
  $(error "http://ffmpeg.org is required - install libavcodec-dev, libswscale-dev, etc")
endif

ifeq ($(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --exists libpng || echo no), no)
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
  ../libharvid/libharvid.a

ifneq ($(shell which xxd),)
FLAGS+=-DXXDI

logo.c: ../doc/harvid.jpg
	xxd -i ../doc/harvid.jpg > logo.c

seek.c: ../doc/seek.js
	xxd -i ../doc/seek.js > seek.c

logo.o: ../doc/harvid.jpg
	xxd -i ../doc/harvid.jpg | $(CC) -x c - -c -o logo.o

seek.o: ../doc/seek.js
	xxd -i ../doc/seek.js | $(CC) -x c - -c -o seek.o

else

logo.o: ../doc/harvid.jpg
	$(LD) -r -b binary -o logo.o ../doc/harvid.jpg

seek.o: ../doc/seek.js
	$(LD) -r -b binary -o seek.o ../doc/seek.js

endif

harvid: $(HARVID_SRC) $(HARVID_H) $(LOGODEP)
	export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH);\
	$(CC) -o $(@) $(CFLAGS) $(FLAGS) $(HARVID_SRC) $(LOGODEP) $(LDFLAGS) $(LOADLIBES)

clean:
	rm -f harvid logo.c logo.o seek.c seek.o cscope.* tags

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

install-lib:

uninstall-lib:

.PHONY: all install uninstall install-man uninstall-man install-bin uninstall-bin install-lib uninstall-lib
