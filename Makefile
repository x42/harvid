SUBDIRS = src doc

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
  ARCHFLAGS+=-headerpad_max_install_names
  else
  ARCHLIBES=-lrt -lpthread
  endif
endif

export PKG_CONFIG_PATH
export LDFLAGS
export CC
export ARCHFLAGS
export ARCHLIBES


default: all

$(SUBDIRS)::
	$(MAKE) -C $@ $(MAKECMDGOALS)

all clean man install uninstall install-bin install-man uninstall-bin uninstall-man: $(SUBDIRS)

dist:
	git archive --format=tar --prefix=harvid-$(VERSION)/ HEAD | gzip -9 > harvid-$(VERSION).tar.gz

.PHONY: clean all subdirs install uninstall dist install-bin install-man uninstall-bin uninstall-man
