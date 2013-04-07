include ../common.mak

FLAGS=
FLAGS+=$(ARCHINCLUDES) $(ARCHFLAGS)
FLAGS+=`pkg-config --cflags libavcodec libavformat libavutil libswscale`
LIBHARVID_OBJECTS = \
  decoder_ctrl.o \
  ffdecoder.o \
  frame_cache.o \
  image_cache.o \
  timecode.o \
  vinfo.o

LIBHARVID_H = \
  decoder_ctrl.h \
  ffdecoder.h \
  frame_cache.h \
  image_cache.h\
  ffcompat.h \
  timecode.h \
  vinfo.h 

all: libharvid.a libharvid.$(LIBEXT)

libharvid.a: $(LIBHARVID_OBJECTS) $(LIBHARVID_H)
ifeq ($(UNAME),Darwin)
	/usr/bin/libtool -static -o libharvid.a $(LIBHARVID_OBJECTS)
else
	$(AR) cru libharvid.a $(LIBHARVID_OBJECTS)
	$(RANLIB) libharvid.a
endif

libharvid.$(LIBEXT): $(LIBHARVID_OBJECTS) $(LIBHARVID_H)
	$(NM) $(LIBHARVID_OBJECTS) \
	  | sed -n -e 's/^.*[ ]\([ABCDGIRSTW][ABCDGIRSTW]*\)[ ][ ]*\([_A-Za-z][_A-Za-z0-9]*\)$$/\1 \2 \2/p' \
	  | sed '/ __gnu_lto/d' | sed 's/.* //' | sed 's/^_//g' \
	  | sort | uniq \
	  | grep -E -e "^(dctrl_|vcache_|jvi_|ff_cleanup|ff_initialize|icache_).*" \
	  > .libharvid.sym
ifeq ($(UNAME),mingw)
	echo "asm (\".section .drectve\");" > .libharvid_dll.c
	sed -e "s/\(.*\)/asm (\".ascii \\\\\" -export:\1\\\\\"\");/" .libharvid.sym >> .libharvid_dll.c
	$(CC) -o libharvid.$(LIBEXT) -shared $(CFLAGS) $(FLAGS) $(LIBHARVID_OBJECTS) \
	  .libharvid_dll.c dlog_null.c \
	  $(LDFLAGS) `pkg-config --libs libavcodec libavformat libavutil libswscale` $(ARCHLIBES)
	$(STRIP) libharvid.$(LIBEXT)
else
ifeq ($(UNAME),Darwin)
	sed 's,^,_,' .libharvid.sym > .libharvid.expsym
	$(CC) -dynamiclib -Wl,-undefined -Wl,dynamic_lookup -o libharvid.$(LIBEXT) $(LIBHARVID_OBJECTS) $(CFLAGS) $(FLAGS) -install_name $(libdir)/libltc.0.$(LIBEXT) -Wl,-single_module -Wl,-exported_symbols_list,.libharvid.expsym
	dsymutil libharvid.$(LIBEXT)
else
	echo "{ global:" > .libharvid.ver
	sed -e "s/\(.*\)/\1;/" .libharvid.sym >> .libharvid.ver
	echo "local: *; };" >> .libharvid.ver
	$(CC) -o libharvid.$(LIBEXT) -fPIC -shared $(CFLAGS) $(FLAGS) $(LIBHARVID_OBJECTS) -Wl,-soname -Wl,libharvid.so.0 -Wl,-version-script -Wl,.libharvid.ver
	$(STRIP) libharvid.$(LIBEXT)
endif
endif

%.o: %.c $(LIBHARVID_H)
	$(CC) -c -o $(@) $(CFLAGS) $(FLAGS) $<


install-lib: libharvid.a libharvid.$(LIBEXT)
	install -d $(DESTDIR)$(libdir)/pkgconfig/
	sed 's!@PREFIX@!'$(PREFIX)'!;s!@LIBDIR@!'$(libdir)'!;s!@VERSION@!'$(VERSION)'!' harvid.pc.in > harvid.pc
	install -m 644 harvid.pc $(DESTDIR)$(libdir)/pkgconfig/
	install -m 644 libharvid.$(LIBEXT) $(DESTDIR)$(libdir)/
	install -m 644 libharvid.a $(DESTDIR)$(libdir)/
	install -d $(DESTDIR)$(PREFIX)/include/harvid/
	install -m 644 harvid.h $(LIBHARVID_H) $(DESTDIR)$(PREFIX)/include/harvid/


clean:
	rm -f *.o libharvid.so libharvid.dll libharvid.dylib harvid.pc .libharvid.sym .libharvid.ver .libharvid_dll.c 

install:

uninstall:

install-man:

uninstall-man:

install-bin:

uninstall-bin:

man:

clean:

.PHONY: all install uninstall install-man uninstall-man install-bin uninstall-bin
