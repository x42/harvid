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
 librtmp.a \
 libgmp.a \
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
 libschroedinger-1.0.a \
 liborc-0.4.a \
 libgsm.a \
 libbluray.a \
 libxvidcore.a \
 libopus.a \
 libbz2.a \
 libvpx.a \
 libopenjpeg.a \
 libx264.a \
 libX11.a \
 libxcb.a \
 libXau.a \
 libXdmcp.a \
 libjpeg.a \
 libz.a \
 "
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

# compile harvid
make -C src clean logo.o seek.o
mkdir -p tmp
gcc -DNDEBUG -DICSARCH=\"Linux\" -DICSVERSION=\"${VERSION}\" \
  -Wall -O2 \
  -o tmp/$OUTFN src/*.c src/logo.o src/seek.o \
	`pkg-config --cflags libavcodec libavformat libavutil libpng libswscale` \
	${CFLAGS} \
	${LIBF}/libavformat.a \
	${LIBF}/libavcodec.a \
	${LIBF}/libswscale.a \
	${LIBF}/libavdevice.a \
	${LIBF}/libavutil.a \
	\
	$SLIBS \
	-lm -ldl -pthread -lstdc++ \
|| exit 1

strip tmp/$OUTFN
ls -lh tmp/$OUTFN
ldd tmp/$OUTFN

# give any arg to disable bundle
test -n "$1" && exit 1

# build .tgz bundle
rm -rf $RESULT/$OUTFN $RESULT/$OUTFN.tgz
mkdir $RESULT/$OUTFN
cp tmp/$OUTFN $RESULT/$OUTFN/harvid
cp README.md $RESULT/$OUTFN/README
cp doc/harvid.1 $RESULT/$OUTFN/harvid.1
if test -f $BINF/ffmpeg_s; then
	cp $BINF/ffmpeg_s $RESULT/$OUTFN/ffmpeg
fi
if test -f $BINF/ffprobe_s; then
	cp $BINF/ffprobe_s $RESULT/$OUTFN/ffprobe
fi
cd $RESULT/ ; tar czf $RESULT/$OUTFN.tgz $OUTFN ; cd -
rm -rf $RESULT/$OUTFN
ls -lh $RESULT/$OUTFN.tgz

# ..and copy the bundle to the local gh-pages branch
test -d site/releases/ || exit
mv $RESULT/$OUTFN.tgz site/releases/
ls -lh site/releases/$OUTFN.tgz
