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

ifneq ($(XWIN),)
	LIBHARVID_OBJECTS += snprintf.o
	FLAGS += -DSNPRINTF_LONGLONG_SUPPORT -DHAVE_SNPRINTF -DPREFER_PORTABLE_SNPRINTF
endif

all: libharvid.a

libharvid.a: $(LIBHARVID_OBJECTS) $(LIBHARVID_H)
ifeq ($(UNAME),Darwin)
	/usr/bin/libtool -static -o libharvid.a $(LIBHARVID_OBJECTS)
else
	$(AR) cru libharvid.a $(LIBHARVID_OBJECTS)
	$(RANLIB) libharvid.a
endif

.libharvid.sym: $(LIBHARVID_OBJECTS) $(LIBHARVID_H)
	$(NM) $(LIBHARVID_OBJECTS) \
	  | sed -n -e 's/^.*[ ]\([ABCDGIRSTW][ABCDGIRSTW]*\)[ ][ ]*\([_A-Za-z][_A-Za-z0-9]*\)$$/\1 \2 \2/p' \
	  | sed '/ __gnu_lto/d' | sed 's/.* //' | sed 's/^_//g' \
	  | sort | uniq \
	  | grep -E -e "^(dctrl_|vcache_|jvi_|ff_cleanup|ff_initialize|icache_).*" \
	  > .libharvid.sym

libharvid.dll: $(LIBHARVID_OBJECTS) $(LIBHARVID_H) .libharvid.sym dlog_null.c
	echo "asm (\".section .drectve\");" > .libharvid_dll.c
	sed -e "s/\(.*\)/asm (\".ascii \\\\\" -export:\1\\\\\"\");/" .libharvid.sym >> .libharvid_dll.c
	export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH);\
	$(CC) -o libharvid.$(LIBEXT) \
	  -shared \
	  $(LIBHARVID_OBJECTS) \
	  $(CFLAGS) $(FLAGS) \
	  .libharvid_dll.c dlog_null.c \
	  $(LDFLAGS) `pkg-config --libs libavcodec libavformat libavutil libswscale` $(ARCHLIBES)
	$(STRIP) libharvid.$(LIBEXT)

libharvid.dylib: $(LIBHARVID_OBJECTS) $(LIBHARVID_H) .libharvid.sym
	sed 's,^,_,' .libharvid.sym > .libharvid.expsym
	export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH);\
	$(CC) -o libharvid.$(LIBEXT) \
	  -dynamiclib \
	  $(LIBHARVID_OBJECTS) \
	  $(CFLAGS) $(FLAGS) \
	  -install_name $(libdir)/libharvid.0.$(LIBEXT) \
	  -Wl,-undefined -Wl,dynamic_lookup \
	  -Wl,-single_module -Wl,-exported_symbols_list,.libharvid.expsym
	dsymutil libharvid.$(LIBEXT)

libharvid.so: $(LIBHARVID_OBJECTS) $(LIBHARVID_H) .libharvid.sym
	echo "{ global:" > .libharvid.ver
	sed -e "s/\(.*\)/\1;/" .libharvid.sym >> .libharvid.ver
	echo "local: *; };" >> .libharvid.ver
	export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH);\
	$(CC) -o libharvid.$(LIBEXT) \
	  -fPIC -shared \
	  $(LIBHARVID_OBJECTS) \
	  $(CFLAGS) $(FLAGS) \
	  -Wl,-soname -Wl,libharvid.so.0 -Wl,-version-script -Wl,.libharvid.ver
	$(STRIP) libharvid.$(LIBEXT)


%.o: %.c $(LIBHARVID_H)
	export PKG_CONFIG_PATH=$(PKG_CONFIG_PATH);\
	$(CC) -c -o $(@) $(CFLAGS) $(FLAGS) $<

install-lib: libharvid.a libharvid.$(LIBEXT)
	install -d $(DESTDIR)$(libdir)/pkgconfig/
	sed 's!@PREFIX@!'$(PREFIX)'!;s!@LIBDIR@!'$(libdir)'!;s!@VERSION@!'$(VERSION)'!' harvid.pc.in > harvid.pc
	install -m 644 harvid.pc $(DESTDIR)$(libdir)/pkgconfig/
	install -m 644 libharvid.$(LIBEXT) $(DESTDIR)$(libdir)/
	install -m 644 libharvid.a $(DESTDIR)$(libdir)/
	install -d $(DESTDIR)$(includedir)/harvid/
	install -m 644 harvid.h $(LIBHARVID_H) $(DESTDIR)$(includedir)/harvid/
	install -d $(DESTDIR)$(hdocdir)/examples
	install -m 644 dlog_null.c $(DESTDIR)$(hdocdir)/examples/
	gzip -9 $(DESTDIR)$(hdocdir)/examples/dlog_null.c

uninstall-lib:
	rm -f $(DESTDIR)$(libdir)/pkgconfig/harvid.pc
	rm -f $(DESTDIR)$(libdir)/libharvid.$(LIBEXT)
	rm -f $(DESTDIR)$(libdir)/libharvid.a
	rm -rf $(DESTDIR)$(includedir)/harvid/
	rm -f $(DESTDIR)$(hdocdir)/examples/dlog_null.c.gz
	-rmdir $(DESTDIR)$(libdir)/pkgconfig
	-rmdir $(DESTDIR)$(libdir)
	-rmdir $(DESTDIR)$(hdocdir)/examples
	-rmdir $(DESTDIR)$(hdocdir)
	-rmdir $(DESTDIR)$(docdir)
	-rmdir $(DESTDIR)$(includedir)

clean:
	rm -f *.o libharvid.a libharvid.so libharvid.dll libharvid.dylib harvid.pc .libharvid.sym .libharvid.ver .libharvid_dll.c 

install:

uninstall:

install-man:

uninstall-man:

install-bin:

uninstall-bin:

man: libharvid.a

clean:

.PHONY: all install uninstall install-man uninstall-man install-bin uninstall-bin install-lib uninstall-lib
