#!/bin/sh

if [ "$(id -u)" != "0" ]; then
	echo "This script must be run as root in pbuilder" 1>&2
  echo "e.g  sudo ARCH=amd64 DIST=wheezy cowbuilder $0"
	exit 1
fi

PFX=$HOME/local

apt-get update

apt-get -y install git build-essential yasm \
	libasound2-dev libass-dev libbluray-dev libgmp3-dev \
	libbz2-dev libfreetype6-dev libgsm1-dev liblzo2-dev \
	libmp3lame-dev libopenjpeg-dev libopus-dev librtmp-dev \
	libschroedinger-dev libspeex-dev libtheora-dev \
	libvorbis-dev libvpx-dev libx264-dev libxfixes-dev \
	libxvidcore-dev zlib1g-dev zlib1g-dev \
	libpng12-dev libjpeg8-dev \

cd /usr/src/
git clone -b master --depth 0 git://source.ffmpeg.org/ffmpeg
git clone -b master --depth 0 git://github.com/x42/harvid.git

cd /usr/src/ffmpeg
./configure --enable-gpl \
	--enable-libmp3lame --enable-libx264 --enable-libxvid --enable-libtheora  --enable-libvorbis \
	--enable-libvpx --enable-libopenjpeg --enable-libopus --enable-libschroedinger \
	--enable-libspeex --enable-libbluray --enable-libgsm \
	--disable-vaapi --disable-x11grab \
	--disable-devices \
	--enable-shared --enable-static --prefix=$PFX $@

make -j4 || exit 1
make install

cd /usr/src/ffmpeg
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

SLIBS=""
for SLIB in $LIBDEPS; do
	echo -n "searching $SLIB.."
	SL=`find /usr/lib -name "$SLIB"`
	if test -z "$SL"; then
		echo " not found."
		exit 1
	else
		echo
	fi
	SLIBS="$SLIBS $SL"
done

LIBF=$PFX/lib/
export PKG_CONFIG_PATH=${LIBF}/pkgconfig

gcc -o ffmpeg_s ffmpeg_opt.o ffmpeg_filter.o ffmpeg.o cmdutils.o \
	${LIBF}libavformat.a \
	${LIBF}libavdevice.a \
	${LIBF}libavfilter.a \
	${LIBF}libavcodec.a \
	${LIBF}libswscale.a \
	${LIBF}libswresample.a \
	${LIBF}libpostproc.a \
	${LIBF}libavutil.a \
	\
	$SLIBS \
	-lrt -lm -ldl -pthread -lstdc++ \
|| exit

gcc -o ffprobe_s ffprobe.o cmdutils.o \
	${LIBF}libavformat.a \
	${LIBF}libavdevice.a \
	${LIBF}libavfilter.a \
	${LIBF}libavcodec.a \
	${LIBF}libswscale.a \
	${LIBF}libswresample.a \
	${LIBF}libpostproc.a \
	${LIBF}libavutil.a \
	\
	$SLIBS \
	-lrt -lm -ldl -pthread -lstdc++ \
|| exit

strip ffmpeg_s
strip ffprobe_s

install -m755  ffmpeg_s $PFX/bin/
install -m755  ffprobe_s $PFX/bin/

cd /usr/src/harvid
./x-static.sh

# TODO copy out of pbuilder..
ls -l /tmp/harvid*.tgz
