#!/bin/bash
## compile a statically linked version of harvid for linux bundles ##

# this requires a build of ffmpeg; see x-pbuildstatic.sh

VERSION=$(git describe --tags HEAD || echo "X.X.X")
TRIPLET=$(gcc -print-multiarch)
OUTFN=harvid-$TRIPLET-$VERSION

PFX=$HOME/local
LIBF=$PFX/lib
BINF=$PFX/bin

export PKG_CONFIG_PATH=${LIBF}/pkgconfig

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
# paths to static libs
SLIBS=""
for SLIB in $LIBDEPS; do
	echo "searching $SLIB.."
	SL=`find /usr/lib -name "$SLIB"`
	if test -z "$SL"; then
		echo "not found."
		exit
	fi
	SLIBS="$SLIBS $SL"
done


make -C src clean logo.o seek.o
mkdir -p tmp
gcc -DNDEBUG -DICSARCH=\"Linux\" -DICSVERSION=\"${VERSION}\" \
  -Wall -O2 \
  -o tmp/$OUTFN src/*.c src/logo.o src/seek.o \
	`pkg-config --cflags libavcodec libavformat libavutil libpng libswscale` \
	${CFLAGS} \
	${LIBF}libavformat.a \
	${LIBF}libavcodec.a \
	${LIBF}libswscale.a \
	${LIBF}libavdevice.a \
	${LIBF}libavutil.a \
	\
	$SLIBS \
	-lm -ldl -pthread -lstdc++ \
|| exit

strip tmp/$OUTFN
ls -lh tmp/$OUTFN
ldd tmp/$OUTFN

###
test -n "$1" && exit
###

rm -rf /tmp/$OUTFN /tmp/$OUTFN.tgz
mkdir /tmp/$OUTFN
cp tmp/$OUTFN /tmp/$OUTFN/harvid
cp README.md /tmp/$OUTFN/README
cp doc/harvid.1 /tmp/$OUTFN/harvid.1
if test -f $BINF/ffmpeg_s; then
	cp $BINF/ffmpeg_s /tmp/$OUTFN/ffmpeg
fi
if test -f $BINF/ffprobe_s; then
	cp $BINF/ffprobe_s /tmp/$OUTFN/ffprobe
fi
cd /tmp/ ; tar czf /tmp/$OUTFN.tgz $OUTFN ; cd -
rm -rf /tmp/$OUTFN
ls -lh /tmp/$OUTFN.tgz

test -d site/releases/ || exit
mv /tmp/$OUTFN.tgz site/releases/
ls -lh site/releases/$OUTFN.tgz
