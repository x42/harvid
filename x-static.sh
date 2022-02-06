#!/bin/bash
## compile a statically linked version of harvid for linux
## this requires a static build of ffmpeg; see x-pbuildstatic.sh
##

#path to the static ffmpeg installation
: ${PFX=$HOME/local}
#path to output directory -- /harvid*.tgz will end up there
: ${RESULT=/tmp}

VERSION=$(git describe --tags HEAD || echo "X.X.X")
TRIPLET=$(gcc -print-multiarch)
OUTFN=harvid-$TRIPLET-$VERSION

LIBF=$PFX/lib
BINF=$PFX/bin
export PKG_CONFIG_PATH=${LIBF}/pkgconfig

# ffmpeg needs this libs
LIBDEPS=" \
 libpng12.a \
 libjpeg.a \
 libmp3lame.a \
 libspeex.a \
 libtheoraenc.a \
 libtheoradec.a \
 libogg.a \
 libvorbis.a \
 libvorbisenc.a \
 libvorbisfile.a \
 libgsm.a \
 libbluray.a \
 libxvidcore.a \
 libbz2.a \
 libopenjpeg.a \
 libx264.a \
 libz.a \
 "

if test "`hostname`" == "soyuz"; then
	LIBDEPS="$LIBDEPS \
 libX11.a \
 libxcb.a \
 libXau.a \
 libXdmcp.a \
 "
fi

# resolve paths to static libs on the system
SLIBS=""
for SLIB in $LIBDEPS; do
	echo "searching $SLIB.."
	SL=`find /usr/lib -name "$SLIB"`
	if test -z "$SL"; then
		echo "not found."
		exit 1
	fi
	SLIBS="$SLIBS $SL"
done

LIBHARVID_SRC=" \
 libharvid/decoder_ctrl.c \
 libharvid/ffdecoder.c \
 libharvid/frame_cache.c \
 libharvid/image_cache.c \
 libharvid/timecode.c \
 libharvid/vinfo.c \
 "

# compile harvid
make -C src clean logo.o seek.o
mkdir -p tmp
gcc -DNDEBUG -DICSARCH=\"Linux\" -DICSVERSION=\"${VERSION}\" \
  -Wall -O2 -DXXDI \
  -o tmp/$OUTFN -Ilibharvid src/*.c ${LIBHARVID_SRC} src/logo.o src/seek.o \
	`pkg-config --cflags libavcodec libavformat libavutil libpng libswscale` \
	${CFLAGS} -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 \
	${LIBF}/libavformat.a \
	${LIBF}/libavcodec.a \
	${LIBF}/libswscale.a \
	${LIBF}/libavdevice.a \
	${LIBF}/libswresample.a \
	${LIBF}/libavutil.a \
	\
	$SLIBS \
	-lm -lrt -ldl -pthread -lstdc++ \
|| exit 1

strip tmp/$OUTFN
ls -lh tmp/$OUTFN
ldd tmp/$OUTFN

# give any arg to disable bundle
test -n "$1" && exit 0

# build .tgz bundle
rm -rf $RESULT/$OUTFN $RESULT/$OUTFN.tgz
mkdir $RESULT/$OUTFN
cp tmp/$OUTFN $RESULT/$OUTFN/harvid
cp README.md $RESULT/$OUTFN/README
cp doc/harvid.1 $RESULT/$OUTFN/harvid.1
if test -f $BINF/ffmpeg_s; then
	cp $BINF/ffmpeg_s $RESULT/$OUTFN/ffmpeg_harvid
fi
if test -f $BINF/ffprobe_s; then
	cp $BINF/ffprobe_s $RESULT/$OUTFN/ffprobe_harvid
fi
cd $RESULT/ ; tar czf $RESULT/$OUTFN.tgz $OUTFN ; cd -
rm -rf $RESULT/$OUTFN
ls -lh $RESULT/$OUTFN.tgz

# ..and copy the bundle to the local gh-pages branch
test -d site/releases/ || exit 0
mv $RESULT/$OUTFN.tgz site/releases/
ls -lh site/releases/$OUTFN.tgz
