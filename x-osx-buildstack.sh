#!/bin/bash

# we keep a copy of the sources here:
: ${SRCDIR=/var/tmp/src_cache}
# actual build location
: ${BUILDD=$HOME/src/hv_build}
# target install dir:
: ${PREFIX=$HOME/src/hv_stack}
# concurrency
: ${MAKEFLAGS="-j2"}

case `sw_vers -productVersion | cut -d'.' -f1,2` in
	"10.6")
		echo "Snow Leopard"
		HVARCH="-arch i386 -arch x86_64"
		OSXCOMPAT="-isysroot /Developer/SDKs/MacOSX10.5.sdk -mmacosx-version-min=10.5"
		;;
	*)
		echo "**UNTESTED OSX VERSION**"
		echo "if it works, please report back :)"
		HVARCH="-arch i386 -arch x86_64"
		OSXCOMPAT="-mmacosx-version-min=10.5"
		;;
	esac

################################################################################
set -e

# start with a clean slate:
if test -z "$NOCLEAN"; then
	rm -rf ${BUILDD}
	rm -rf ${PREFIX}
fi

mkdir -p ${SRCDIR}
mkdir -p ${PREFIX}
mkdir -p ${BUILDD}

unset PKG_CONFIG_PATH
export PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig
export PREFIX
export SRCDIR

OSXCOMPAT="$OSXCOMPAT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64"

export PATH=${PREFIX}/bin:${HOME}/bin:/usr/local/git/bin/:/usr/bin:/bin:/usr/sbin:/sbin

function autoconfconf {
	set -e
	echo "======= $(pwd) ======="
	CFLAGS="${HVARCH}${OSXCOMPAT:+ $OSXCOMPAT}" \
		CXXFLAGS="${HVARCH}${OSXCOMPAT:+ $OSXCOMPAT}" \
		LDFLAGS="${HVARCH}${OSXCOMPAT:+ $OSXCOMPAT} -headerpad_max_install_names" \
		./configure --disable-dependency-tracking --prefix=$PREFIX --enable-shared "$@"
}

function autoconfbuild {
	set -e
	autoconfconf "$@"
	make $MAKEFLAGS && make install
}

function download {
echo "--- Downloading.. $2"
test -f ${SRCDIR}/$1 || curl -kL -o ${SRCDIR}/$1 $2
}

function src {
download ${1}.${2} $3
cd ${BUILDD}
rm -rf $1
tar xf ${SRCDIR}/${1}.${2}
cd $1
}

################################################################################
src pkg-config-0.28 tar.gz http://pkgconfig.freedesktop.org/releases/pkg-config-0.28.tar.gz
./configure --prefix=$PREFIX --with-internal-glib
make $MAKEFLAGS
make install

################################################################################
src libiconv-1.14 tar.gz ftp://ftp.gnu.org/gnu/libiconv/libiconv-1.14.tar.gz
autoconfbuild --with-included-gettext --with-libiconv-prefix=$PREFIX

################################################################################
src zlib-1.2.7 tar.gz ftp://ftp.simplesystems.org/pub/libpng/png/src/history/zlib/zlib-1.2.7.tar.gz
./configure --prefix=$PREFIX --archs="$HVARCH"
make $MAKEFLAGS
make install

################################################################################
src libpng-1.6.37 tar.gz https://downloads.sourceforge.net/project/libpng/libpng16/1.6.37/libpng-1.6.37.tar.gz
autoconfbuild

################################################################################
download jpegsrc.v9a.tar.gz http://www.ijg.org/files/jpegsrc.v9a.tar.gz
cd ${BUILDD}
tar xzf ${SRCDIR}/jpegsrc.v9a.tar.gz
cd jpeg-9a
autoconfbuild

################################################################################
src libogg-1.3.2 tar.gz http://downloads.xiph.org/releases/ogg/libogg-1.3.2.tar.gz
autoconfbuild

################################################################################
src libvorbis-1.3.4 tar.gz http://downloads.xiph.org/releases/vorbis/libvorbis-1.3.4.tar.gz
autoconfbuild --disable-examples --disable-oggtest --with-ogg=${PREFIX}

################################################################################
src libtheora-1.1.1 tar.bz2 http://downloads.xiph.org/releases/theora/libtheora-1.1.1.tar.bz2
cp configure configure.bak
sed -i '' 's/-fforce-addr //' configure
autoconfbuild --disable-sdltest --disable-vorbistest --disable-oggtest --disable-asm --disable-examples --with-ogg=${PREFIX} --with-vorbis=${PREFIX}

################################################################################
src yasm-1.2.0 tar.gz http://www.tortall.net/projects/yasm/releases/yasm-1.2.0.tar.gz
autoconfbuild

################################################################################
function x264build {
CFLAGS="-arch $1 ${OSXCOMPAT}" \
LDFLAGS="-arch $1 ${OSXCOMPAT} -headerpad_max_install_names" \
./configure --host=$1-macosx-darwin --enable-shared --disable-cli
make $MAKEFLAGS
DYL=`ls libx264.*.dylib`
cp ${DYL} ${DYL}-$1
}

### ftp://ftp.videolan.org/pub/x264/snapshots/last_stable_x264.tar.bz2
download x264-snapshot-20171224-2245-stable.tar.bz2 http://download.videolan.org/pub/videolan/x264/snapshots/x264-snapshot-20171224-2245-stable.tar.bz2
cd ${BUILDD}
tar xjf  ${SRCDIR}/x264-snapshot-20171224-2245-stable.tar.bz2
cd x264*
x264build i386
make install prefix=${PREFIX}
make clean
x264build x86_64
if echo "$HVARCH" | grep -q "ppc"; then
	make clean
	x264build ppc
fi

DYL=`ls libx264.*.dylib`
lipo -create -output ${PREFIX}/lib/${DYL} ${DYL}-*
install_name_tool -id ${PREFIX}/lib/${DYL} ${PREFIX}/lib/${DYL}

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

rm -rf ${PREFIX}/fflipo
mkdir ${PREFIX}/fflipo

cd ${BUILDD}/ffmpeg-${FFVERSION}/

./configure --prefix=${PREFIX} \
	--enable-libx264 --enable-libtheora --enable-libvorbis --enable-libmp3lame \
	--enable-shared --enable-gpl --disable-static --disable-debug --disable-doc \
	--disable-ffplay --disable-iconv \
	--disable-sdl2 --disable-coreimage \
	--arch=x86_32 --target-os=darwin --cpu=i686 --enable-cross-compile \
	--extra-cflags="-arch i386 ${OSXCOMPAT}  -I${PREFIX}/include" \
	--extra-ldflags="-arch i386 ${OSXCOMPAT} -L${PREFIX}/lib -headerpad_max_install_names"
make $MAKEFLAGS
make install

find . -iname "*dylib" -type f -exec echo cp -v {} ${PREFIX}/fflipo/\`basename {}\`-i386 \; | bash -
cp ffprobe ${PREFIX}/fflipo/ffprobe-i386
cp ffmpeg ${PREFIX}/fflipo/ffmpeg-i386
make clean

cd ${BUILDD}/ffmpeg-${FFVERSION}/
./configure --prefix=${PREFIX} \
	--enable-libx264 \
	--enable-libtheora --enable-libvorbis --enable-libmp3lame \
	--enable-shared --enable-gpl --disable-static --disable-debug --disable-doc \
	--disable-ffplay --disable-iconv \
	--disable-sdl2 --disable-coreimage \
	--arch=x86_64 \
	--extra-cflags="-arch x86_64 ${OSXCOMPAT}  -I${PREFIX}/include" \
	--extra-ldflags="-arch x86_64 ${OSXCOMPAT} -L${PREFIX}/lib -headerpad_max_install_names"
make $MAKEFLAGS
find . -iname "*dylib" -type f -exec echo cp -v {} ${PREFIX}/fflipo/\`basename {}\`-x86_64 \; | bash -
cp ffprobe ${PREFIX}/fflipo/ffprobe-x86_64
cp ffmpeg ${PREFIX}/fflipo/ffmpeg-x86_64
make clean

if echo "$HVARCH" | grep -q "ppc"; then
cd ${BUILDD}/ffmpeg-${FFVERSION}/
./configure --prefix=${PREFIX} \
	--enable-libx264 --enable-libtheora --enable-libvorbis --enable-libmp3lame \
	--enable-shared --enable-gpl --disable-static --disable-debug --disable-doc \
	--disable-ffplay --disable-iconv \
	--disable-sdl2 --disable-coreimage \
	--arch=ppc \
	--extra-cflags="-arch ppc ${OSXCOMPAT}  -I${PREFIX}/include" \
	--extra-ldflags="-arch ppc ${OSXCOMPAT} -L${PREFIX}/lib -headerpad_max_install_names"
make $MAKEFLAGS
find . -iname "*dylib" -type f -exec echo cp -v {} ${PREFIX}/fflipo/\`basename {}\`-ppc \; | bash -
cp ffprobe ${PREFIX}/fflipo/ffprobe-ppc
cp ffmpeg ${PREFIX}/fflipo/ffmpeg-ppc
fi

for file in ${PREFIX}/fflipo/*.dylib-i386; do
  BN=$(basename $file -i386)
  TN=$(readlink ${PREFIX}/lib/${BN})
  lipo -create -output ${PREFIX}/lib/${TN} ${PREFIX}/fflipo/${BN}-*
done

lipo -create -o "${PREFIX}/bin/ffmpeg" ${PREFIX}/fflipo/ffmpeg-*
lipo -create -o "${PREFIX}/bin/ffprobe" ${PREFIX}/fflipo/ffprobe-*

rm -rf ${PREFIX}/fflipo

################################################################################
if test -n "$DOCLEAN"; then
	rm -rf ${BUILDD}
	mkdir ${BUILDD}
fi
################################################################################
cd ${BUILDD}
rm -rf harvid
git clone -b master --single-branch https://github.com/x42/harvid.git
cd harvid

export HVARCH
export OSXCOMPAT
./x-osx-bundle.sh
