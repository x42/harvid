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

all: libharvid.a

libharvid.a: $(LIBHARVID_OBJECTS) $(LIBHARVID_H)
	$(AR) cru libharvid.a $(LIBHARVID_OBJECTS)
	$(RANLIB) libharvid.a

%.o: %.c $(LIBHARVID_H)
	$(CC) -c -o $(@) $(CFLAGS) $(FLAGS) $<



clean:
	rm -f *.o libharvid.a

install:

uninstall:

install-man:

uninstall-man:

install-bin:

uninstall-bin:

man:

clean:

.PHONY: all install uninstall install-man uninstall-man install-bin uninstall-bin
