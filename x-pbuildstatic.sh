#!/bin/sh
# this script creates a statically linked version
# of ffmpeg, ffprobe and harvid for GNU/Linux
#
# It is intended to run in a pristine chroot or VM of a minimal
# debian system. see http://wiki.debian.org/cowbuilder
#
# This script
#  - git clone the source to $SRC (default /usr/src)
#  - build and install ffmpeg to $PFX (default ~/local/)
#  - build harvid and bundle it to /tmp/harvid*.tgz
#    (/tmp/ is fixed set by x-static.sh )
#


#use environment variables if set for SRC and PFX
: ${SRC=/usr/src}
: ${PFX=$HOME/local}
: ${SRCDIR=/var/tmp/winsrc}

if [ "$(id -u)" != "0" -a -z "$SUDO" ]; then
	echo "This script must be run as root in pbuilder" 1>&2
  echo "e.g sudo cowbuilder --architecture amd64 --distribution wheezy --bindmounts /tmp --execute $0"
	exit 1
fi

#$SUDO apt-get update
$SUDO apt-get -y install git build-essential yasm \
	libass-dev libbluray-dev libgmp3-dev liblzma-dev \
	libbz2-dev libfreetype6-dev libgsm1-dev liblzo2-dev \
	libmp3lame-dev  librtmp-dev libxml2-dev \
	libspeex-dev libtheora-dev \
	libvorbis-dev libx264-dev \
	libxvidcore-dev zlib1g-dev zlib1g-dev \
	libpng-dev libjpeg-dev curl vim-common

mkdir -p ${SRCDIR}
mkdir -p ${SRC}

cd $SRC
GIT_SSL_NO_VERIFY=true git clone -b master --single-branch https://github.com/x42/harvid.git

set -e

FFVERSION=5.0
test -f ${SRCDIR}/ffmpeg-${FFVERSION}.tar.bz2 \
	|| curl -k -L -o ${SRCDIR}/ffmpeg-${FFVERSION}.tar.bz2 http://www.ffmpeg.org/releases/ffmpeg-${FFVERSION}.tar.bz2
tar xjf ${SRCDIR}/ffmpeg-${FFVERSION}.tar.bz2

cd $SRC/harvid
VERSION=$(git describe --tags HEAD)
git archive --format=tar --prefix=harvid-${VERSION}/ HEAD | gzip -9 > /tmp/harvid-${VERSION}.tar.gz

cd $SRC/ffmpeg-${FFVERSION}
if test -d .git; then
	git archive --format=tar --prefix=ffmpeg-${FFVERSION}/ HEAD | gzip -9 > /tmp/ffmpeg-${FFVERSION}.tar.gz
fi

./configure --enable-gpl \
	--enable-libmp3lame --enable-libx264 --enable-libxvid --enable-libtheora  --enable-libvorbis \
	--enable-libspeex --enable-libbluray --enable-libgsm \
	--disable-vaapi --disable-devices \
	--extra-cflags="-D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64" \
	--enable-shared --enable-static --prefix=$PFX $@

make -j4
make install

LIBDEPS=" \
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
 libx264.a \
 libfreetype.a \
 libxml2.a \
 libpng.a \
 libbz2.a \
 liblzma.a \
 libz.a \
 "

SLIBS=""
for SLIB in $LIBDEPS; do
	echo -n "searching $SLIB.."
	SL=`find /usr/lib -name "$SLIB"`
	if test -z "$SL"; then
		echo " '$SLIB' not found."
		exit 1
	else
		echo
	fi
	SLIBS="$SLIBS $SL"
done

LIBF=$PFX/lib

cd $SRC/ffmpeg-${FFVERSION}
gcc -o ffmpeg_s \
	fftools/ffmpeg.o fftools/cmdutils.o \
	fftools/ffmpeg_opt.o fftools/ffmpeg_filter.o fftools/ffmpeg_hw.o \
	${LIBF}/libavformat.a \
	${LIBF}/libavdevice.a \
	${LIBF}/libavfilter.a \
	${LIBF}/libavcodec.a \
	${LIBF}/libswscale.a \
	${LIBF}/libswresample.a \
	${LIBF}/libpostproc.a \
	${LIBF}/libavutil.a \
	\
	$SLIBS \
	-lrt -lm -ldl -pthread -lstdc++ \

gcc -o ffprobe_s fftools/ffprobe.o fftools/cmdutils.o \
	${LIBF}/libavformat.a \
	${LIBF}/libavdevice.a \
	${LIBF}/libavfilter.a \
	${LIBF}/libavcodec.a \
	${LIBF}/libswscale.a \
	${LIBF}/libswresample.a \
	${LIBF}/libpostproc.a \
	${LIBF}/libavutil.a \
	\
	$SLIBS \
	-lrt -lm -ldl -pthread -lstdc++ \

strip ffmpeg_s
strip ffprobe_s

install -m755  ffmpeg_s $PFX/bin/
install -m755  ffprobe_s $PFX/bin/

cd $SRC/harvid
./x-static.sh

ls -l /tmp/harvid*.tgz
