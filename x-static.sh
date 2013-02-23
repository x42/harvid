#!/bin/bash
## compile a statically linked version of harvid for linux bundles ##

# this requires a special build of ffmpeg/libav*
# git://source.ffmpeg.org/ffmpeg
# ./configure --enable-gpl \
#        --enable-libmp3lame --enable-libx264 --enable-libxvid --enable-libtheora  --enable-libvorbis \
#        --enable-libvpx --enable-libopenjpeg --enable-libopus --enable-libschroedinger \
#        --enable-libspeex --enable-frei0r --enable-libbluray --enable-libgsm \
#        --disable-vaapi --disable-x11grab \
#        --enable-shared --enable-static --prefix=$HOME/local/ $@

VERSION=$(git describe --tags HEAD || echo "X.X.X")
OUTFN=harvid-static-$VERSION

LIB0=/usr/lib/
LIB1=/usr/lib/i386-linux-gnu/
LIBF=$HOME/local/lib/
export PKG_CONFIG_PATH=${LIBF}/pkgconfig

make -C src clean logo.o
mkdir -p tmp
gcc -DNDEBUG -DICSARCH=\"Linux\" -DICSVERSION=\"${VERSION}\" \
  -Wall -O2 \
  -o tmp/$OUTFN src/*.c src/logo.o \
	`pkg-config --cflags libavcodec libavformat libavutil libpng libswscale` \
	${CFLAGS} \
	${LIBF}libavformat.a \
	${LIBF}libavcodec.a \
	${LIBF}libswscale.a \
	${LIBF}libavdevice.a \
	${LIBF}libavutil.a \
	\
	${LIB1}librtmp.a \
  ${LIB1}libgmp.a \
	${LIB1}libpng12.a \
	${LIB0}libjpeg.a \
	${LIB1}libmp3lame.a \
	${LIB0}libspeex.a \
	${LIB0}libtheoraenc.a \
	${LIB0}libtheoradec.a \
	${LIB0}libogg.a \
	${LIB0}libvorbis.a \
	${LIB0}libvorbisenc.a \
	${LIB0}libvorbisfile.a \
	${LIB0}libdc1394.a \
	${LIB0}libraw1394.a \
	${LIB1}libschroedinger-1.0.a \
	${LIB0}liborc-0.4.a \
	${LIB0}libgsm.a \
	${LIB1}libbluray.a \
	${LIB1}libxvidcore.a \
	${LIB0}libopus.a \
	${LIB1}libbz2.a \
	${LIB0}libvpx.a \
	${LIB0}libopenjpeg.a \
	${LIB1}libx264.a \
	${LIB1}libX11.a \
	${LIB1}libxcb.a \
	${LIB1}libXau.a \
	${LIB1}libXdmcp.a \
	${LIB0}libjpeg.a \
	${LIB0}libz.a \
	-lm -ldl -pthread -lstdc++ \
|| exit

strip tmp/$OUTFN
ls -lh tmp/$OUTFN
ldd tmp/$OUTFN

###

rm -rf /tmp/$OUTFN /tmp/$OUTFN.tgz
mkdir /tmp/$OUTFN
cp tmp/$OUTFN /tmp/$OUTFN/harvid
cp README.md /tmp/$OUTFN/README
cp doc/harvid.1 /tmp/$OUTFN/harvid.1
cd /tmp/ ; tar czf /tmp/$OUTFN.tgz $OUTFN ; cd -
rm -rf /tmp/$OUTFN
ls -lh /tmp/$OUTFN.tgz

test -d site/releases/ || exit
mv /tmp/$OUTFN.tgz site/releases/
ls -lh site/releases/$OUTFN.tgz
