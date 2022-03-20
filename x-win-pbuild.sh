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

if test "$XARCH" = "x86_64" -o "$XARCH" = "amd64"; then
	echo "Target: 64bit Windows (x86_64)"
	XPREFIX=x86_64-w64-mingw32
	HPREFIX=x86_64
	WARCH=w64
	FFFLAGS="--arch=x86_64 --target-os=mingw64 --cpu=x86_64"
	DEBIANPKGS="mingw-w64"
else
	echo "Target: 32 Windows (i686)"
	XPREFIX=i686-w64-mingw32
	HPREFIX=i386
	WARCH=w32
	FFFLAGS="--arch=i686 --target-os=mingw32 --cpu=i686"
	DEBIANPKGS="gcc-mingw-w64-i686 g++-mingw-w64-i686 mingw-w64-tools mingw-w64"
fi

apt-get -y install build-essential \
	${DEBIANPKGS} \
	wget git autoconf automake pkg-config \
	curl unzip ed yasm \
	nsis nasm xxd

#fixup mingw64 ccache for now
if test -d /usr/lib/ccache -a -f /usr/bin/ccache; then
	export PATH="/usr/lib/ccache:${PATH}"
	cd /usr/lib/ccache
	test -L ${XPREFIX}-gcc || ln -s ../../bin/ccache ${XPREFIX}-gcc
	test -L ${XPREFIX}-g++ || ln -s ../../bin/ccache ${XPREFIX}-g++
fi


cd "$SRC"
git clone -b master --single-branch https://github.com/x42/harvid.git

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

function autoconfconf {
	set -e
echo "======= $(pwd) ======="
PATH=${PREFIX}/bin:/usr/bin:/bin:/usr/sbin:/sbin \
	CPPFLAGS="-I${PREFIX}/include" \
	CFLAGS="-I${PREFIX}/include -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -mstackrealign" \
	CXXFLAGS="-I${PREFIX}/include -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -mstackrealign" \
	LDFLAGS="-L${PREFIX}/lib" \
	./configure --host=${XPREFIX} --build=${HPREFIX}-linux \
	--prefix=$PREFIX "$@"
}

function autoconfbuild {
	set -e
	autoconfconf "$@"
	make $MAKEFLAGS && make install
}

################################################################################
download zlib-1.2.7.tar.gz ftp://ftp.simplesystems.org/pub/libpng/png/src/history/zlib/zlib-1.2.7.tar.gz
cd ${BUILDD}
tar xzf ${SRCDIR}/zlib-1.2.7.tar.gz
cd zlib-1.2.7
make -fwin32/Makefile.gcc PREFIX=${XPREFIX}-
make install -fwin32/Makefile.gcc SHARED_MODE=1 \
	INCLUDE_PATH=${PREFIX}/include \
	LIBRARY_PATH=${PREFIX}/lib \
	BINARY_PATH=${PREFIX}/bin

################################################################################
download libiconv-1.16.tar.gz http://ftpmirror.gnu.org/libiconv/libiconv-1.16.tar.gz
cd ${BUILDD}
tar xzf ${SRCDIR}/libiconv-1.16.tar.gz
cd libiconv-1.16
autoconfbuild --with-included-gettext --with-libiconv-prefix=$PREFIX

################################################################################
download libpng-1.6.35.tar.gz https://downloads.sourceforge.net/project/libpng/libpng16/1.6.35/libpng-1.6.35.tar.gz
cd ${BUILDD}
tar xzf ${SRCDIR}/libpng-1.6.35.tar.gz
cd libpng-1.6.35
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
sed -i -e 's/\r$//' win32/xmingw32/libtheoraenc-all.def
sed -i -e 's/\r$//' win32/xmingw32/libtheoradec-all.def
MAKEFLAGS=-j1 autoconfbuild --enable-shared --disable-sdltest --disable-examples --with-ogg=${PREFIX}

################################################################################
download x264-0bb85e8bb.tar.bz2 https://code.videolan.org/videolan/x264/-/archive/0bb85e8bbc85244d5c8fd300033ca32539b541b7/x264-0bb85e8bbc85244d5c8fd300033ca32539b541b7.tar.bz2

cd ${BUILDD}
tar xjf  ${SRCDIR}/x264-0bb85e8bb.tar.bz2
cd x264*
PATH=${PREFIX}/bin:/usr/bin:/bin:/usr/sbin:/sbin \
	CPPFLAGS="-I${PREFIX}/include" \
	CFLAGS="-I${PREFIX}/include -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -mstackrealign" \
	CXXFLAGS="-I${PREFIX}/include -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -mstackrealign" \
	LDFLAGS="-L${PREFIX}/lib" \
	./configure --host=${XPREFIX} --cross-prefix=${XPREFIX}- --prefix=$PREFIX --enable-shared --disable-cli # --disable-asm
make $MAKEFLAGS && make install

################################################################################
download lame-3.100.tar.gz http://sourceforge.net/projects/lame/files/lame/3.100/lame-3.100.tar.gz/download
cd ${BUILDD}
tar xzf ${SRCDIR}/lame-3.100.tar.gz
cd lame-3.100
autoconfconf
sed -i -e '/lame_init_old/d' include/libmp3lame.sym
sed -i -e 's/frontend //' Makefile
make $MAKEFLAGS && make install

################################################################################
FFVERSION=5.0
download ffmpeg-${FFVERSION}.tar.bz2 http://www.ffmpeg.org/releases/ffmpeg-${FFVERSION}.tar.bz2
cd ${BUILDD}
tar xjf ${SRCDIR}/ffmpeg-${FFVERSION}.tar.bz2
cd ffmpeg-${FFVERSION}/
ed configure << EOF
%s/pkg_config_default="\${cross_prefix}\${pkg_config_default}"/pkg_config_default="\${pkg_config_default}"/
wq
EOF

./configure --prefix=${PREFIX} \
	--disable-ffplay \
	--enable-gpl --enable-shared --disable-static --disable-debug \
	--enable-libx264 --enable-libtheora --enable-libvorbis --enable-libmp3lame \
	--disable-sdl2 \
	--enable-cross-compile --cross-prefix=${XPREFIX}- \
	$FFFLAGS \
	--extra-cflags="-I${PREFIX}/include -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -mstackrealign" \
	--extra-ldflags="-L${PREFIX}/lib -mwindows"

make $MAKEFLAGS && make install


###############################################################################

cd "$SRC"/harvid

export WINPREFIX="$PREFIX"
export XPREFIX
export HPREFIX
export WARCH

./x-win-bundle.sh
