#!/bin/bash
# this script creates a windows32 version of harvid
# cross-compiled on GNU/Linux
#
# It is intended to run in a pristine chroot or VM of a minimal
# debian system. see http://wiki.debian.org/cowbuilder
#

: ${SRC=/usr/src}
: ${SRCDIR=/tmp/winsrc}
: ${PREFIX=$SRC/win-stack}
: ${BUILDD=$SRC/win-build}
: ${MAKEFLAGS=-j4}

if [ "$(id -u)" != "0" -a -z "$SUDO" ]; then
	echo "This script must be run as root in pbuilder" 1>&2
  echo "e.g sudo cowbuilder --architecture amd64 --distribution wheezy --bindmounts /tmp --execute $0"
	exit 1
fi

apt-get -y install build-essential \
	gcc-mingw-w64-i686 g++-mingw-w64-i686 mingw-w64-tools mingw32 \
	wget git autoconf automake pkg-config \
	curl unzip ed yasm \
	nsis

cd "$SRC"
git clone -b master --single-branch git://github.com/x42/harvid.git

set -e

###############################################################################

mkdir -p ${SRCDIR}
mkdir -p ${PREFIX}
mkdir -p ${BUILDD}

unset PKG_CONFIG_PATH
export PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig
export PREFIX
export SRCDIR

function download {
echo "--- Downloading.. $2"
test -f ${SRCDIR}/$1 || curl -k -L -o ${SRCDIR}/$1 $2
}

function autoconfbuild {
echo "======= $(pwd) ======="
PATH=${PREFIX}/bin:/usr/bin:/bin:/usr/sbin:/sbin \
	CPPFLAGS="-I${PREFIX}/include" \
	CFLAGS="-I${PREFIX}/include" \
	CXXFLAGS="-I${PREFIX}/include" \
	LDFLAGS="-L${PREFIX}/lib" \
	./configure --host=i686-w64-mingw32 --build=i386-linux --prefix=$PREFIX --enable-shared $@
  make $MAKEFLAGS && make install
}

################################################################################
download pthreads-w32-2-9-1-release.tar.gz ftp://sourceware.org/pub/pthreads-win32/pthreads-w32-2-9-1-release.tar.gz
cd ${BUILDD}
tar xzf ${SRCDIR}/pthreads-w32-2-9-1-release.tar.gz
cd pthreads-w32-2-9-1-release
make clean GC CROSS=i686-w64-mingw32-
mkdir -p ${PREFIX}/bin
mkdir -p ${PREFIX}/lib
mkdir -p ${PREFIX}/include
cp -vf pthreadGC2.dll ${PREFIX}/bin/
cp -vf libpthreadGC2.a ${PREFIX}/lib/libpthread.a
cp -vf pthread.h sched.h ${PREFIX}/include

################################################################################
download zlib-1.2.7.tar.gz ftp://ftp.simplesystems.org/pub/libpng/png/src/history/zlib/zlib-1.2.7.tar.gz
cd ${BUILDD}
tar xzf ${SRCDIR}/zlib-1.2.7.tar.gz
cd zlib-1.2.7
make -fwin32/Makefile.gcc PREFIX=i686-w64-mingw32-
make install -fwin32/Makefile.gcc SHARED_MODE=1 \
	INCLUDE_PATH=${PREFIX}/include \
	LIBRARY_PATH=${PREFIX}/lib \
	BINARY_PATH=${PREFIX}/bin

################################################################################
download libiconv-1.14.tar.gz ftp://ftp.gnu.org/gnu/libiconv/libiconv-1.14.tar.gz
cd ${BUILDD}
tar xzf ${SRCDIR}/libiconv-1.14.tar.gz
cd libiconv-1.14
autoconfbuild --with-included-gettext --with-libiconv-prefix=$PREFIX

################################################################################
#download libpng-1.6.12.tar.gz ftp://ftp.simplesystems.org/pub/libpng/png/src/libpng16/libpng-1.6.12.tar.gz
download libpng-1.6.12.tar.gz https://downloads.sourceforge.net/project/libpng/libpng16/1.6.12/libpng-1.6.12.tar.gz
cd ${BUILDD}
tar xzf ${SRCDIR}/libpng-1.6.12.tar.gz
cd libpng-1.6.12
autoconfbuild

################################################################################
download jpegsrc.v9a.tar.gz http://www.ijg.org/files/jpegsrc.v9a.tar.gz
cd ${BUILDD}
tar xzf ${SRCDIR}/jpegsrc.v9a.tar.gz
cd jpeg-9a
autoconfbuild

################################################################################
download libogg-1.3.2.tar.gz http://downloads.xiph.org/releases/ogg/libogg-1.3.2.tar.gz
cd ${BUILDD}
tar xzf ${SRCDIR}/libogg-1.3.2.tar.gz
cd libogg-1.3.2
autoconfbuild

################################################################################
download libvorbis-1.3.4.tar.gz http://downloads.xiph.org/releases/vorbis/libvorbis-1.3.4.tar.gz
cd ${BUILDD}
tar xzf ${SRCDIR}/libvorbis-1.3.4.tar.gz
cd libvorbis-1.3.4
autoconfbuild --disable-examples --with-ogg=${PREFIX}

################################################################################
download libtheora-1.1.1.tar.bz2 http://downloads.xiph.org/releases/theora/libtheora-1.1.1.tar.bz2
cd ${BUILDD}
tar xjf ${SRCDIR}/libtheora-1.1.1.tar.bz2
cd libtheora-1.1.1
MAKEFLAGS=-j1 autoconfbuild --disable-sdltest --disable-examples --with-ogg=${PREFIX}

################################################################################
### ftp://ftp.videolan.org/pub/x264/snapshots/last_x264.tar.bz2
### ftp://ftp.videolan.org/pub/x264/snapshots/last_stable_x264.tar.bz2
download x264.tar.bz2 ftp://ftp.videolan.org/pub/x264/snapshots/last_stable_x264.tar.bz2 # XXX
cd ${BUILDD}
#git clone --depth 1 git://git.videolan.org/x264.git
tar xjf  ${SRCDIR}/x264.tar.bz2
cd x264*
PATH=${PREFIX}/bin:/usr/bin:/bin:/usr/sbin:/sbin \
	CPPFLAGS="-I${PREFIX}/include" \
	CFLAGS="-I${PREFIX}/include" \
	CXXFLAGS="-I${PREFIX}/include" \
	LDFLAGS="-L${PREFIX}/lib" \
	./configure --host=i686-w64-mingw32 --cross-prefix=i686-w64-mingw32- --prefix=$PREFIX --enable-shared --disable-cli --disable-asm
make $MAKEFLAGS && make install

################################################################################
download libvpx-v1.3.0.tar.bz2 https://webm.googlecode.com/files/libvpx-v1.3.0.tar.bz2
cd ${BUILDD}
tar xjf ${SRCDIR}/libvpx-v1.3.0.tar.bz2
cd libvpx-v1.3.0
ed vpx/src/svc_encodeframe.c << EOF
%s/MINGW_HAS_SECURE_API/MINGW_HAS_SECURE_APIXXX/
wq
EOF
CC=i686-w64-mingw32-gcc\
	CPPFLAGS="-I${PREFIX}/include" \
	CFLAGS="-I${PREFIX}/include" \
	CXXFLAGS="-I${PREFIX}/include" \
	LDFLAGS="-L${PREFIX}/lib" \
	CROSS=i686-w64-mingw32- ./configure --target=x86-win32-gcc --prefix=$PREFIX \
	--disable-examples --disable-docs --disable-install-bins
make -j4 && make install

################################################################################
download lame-3.99.5.tar.gz http://sourceforge.net/projects/lame/files/lame/3.99/lame-3.99.5.tar.gz/download
cd ${BUILDD}
tar xzf ${SRCDIR}/lame-3.99.5.tar.gz
cd lame-3.99.5
autoconfbuild

################################################################################
FFVERSION=2.2.13
download ffmpeg-${FFVERSION}.tar.bz2 http://www.ffmpeg.org/releases/ffmpeg-${FFVERSION}.tar.bz2
cd ${BUILDD}
tar xjf ${SRCDIR}/ffmpeg-${FFVERSION}.tar.bz2
cd ffmpeg-${FFVERSION}/
ed configure << EOF
%s/jack_jack_h/xxjack_jack_h/
%s/enabled jack_indev/enabled xxjack_indev/
%s/sdl_outdev_deps="sdl"/sdl_outdev_deps="xxxsdl"/
%s/enabled sdl/enabled xxsdl/
%s/pkg_config_default="\${cross_prefix}\${pkg_config_default}"/pkg_config_default="\${pkg_config_default}"/
wq
EOF

./configure --prefix=${PREFIX} \
	--disable-ffserver --disable-ffplay \
	--enable-gpl --enable-shared --disable-static --disable-debug --disable-w32threads \
	--enable-libx264 --enable-libtheora --enable-libvpx --enable-libvorbis --enable-libmp3lame \
	--arch=i686 --target-os=mingw32 --cpu=i686 --enable-cross-compile --cross-prefix=i686-w64-mingw32- \
	--extra-cflags="-I${PREFIX}/include" \
	--extra-ldflags="-L${PREFIX}/lib"
make $MAKEFLAGS && make install


###############################################################################

cd "$SRC"/harvid

export WINPREFIX="$PREFIX"
./x-win-bundle.sh
