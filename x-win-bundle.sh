#!/bin/sh
## cross-compile and package win32 version of harvid ##
## see also x-win-pbuild.sh
## 'configure prefix=~/.wine/drive_c/x-prefix host=i586-mingw32msvc-gcc ...'

#environment variables
: ${WINPREFIX=$HOME/.wine/drive_c/x-prefix}
: ${WINLIB=${WINPREFIX}/lib}
: ${XPREFIX=i686-w64-mingw32}
: ${HPREFIX=i386}
: ${WARCH=w32}

set -e

VERSION=$(git describe --tags HEAD || echo "X.X.X")

if test -z "$NSIDIR"; then
	NSIDIR=$(mktemp -d)
	trap 'rm -rf $NSIDIR' exit
else
	mkdir -p "$NSIDIR"
fi

make XWIN=${XPREFIX} WINPREFIX=${WINPREFIX} clean
make XWIN=${XPREFIX} WINPREFIX=${WINPREFIX} CFLAGS="-DNDEBUG -O2 -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -mstackrealign"

test -n "$1" && exit 0

# NSI package

cp -v src/harvid.exe $NSIDIR/harvid.exe
${XPREFIX}-strip $NSIDIR/harvid.exe

if update-alternatives --query ${XPREFIX}-gcc | grep Value: | grep -q win32; then
	cp -v /usr/lib/gcc/${XPREFIX}/*-win32/libgcc_s_*.dll $NSIDIR
elif update-alternatives --query ${XPREFIX}-gcc | grep Value: | grep -q posix; then
	cp -v /usr/lib/gcc/${XPREFIX}/*-posix/libgcc_s_*.dll $NSIDIR
else
	cp -v /usr/lib/gcc/${XPREFIX}/*/libgcc_s_sjlj-1.dll $NSIDIR
fi

cp -v /usr/${XPREFIX}/lib/libwinpthread-*.dll $NSIDIR

ffdlls="avcodec- avdevice- avfilter- avformat- avutil- libcharset- libiconv- libjpeg- libmp3lame- libogg- libpng16- libtheora- libtheoradec- libtheoraenc- libvorbis- libvorbisenc- libvorbisfile- libx264- postproc- swresample- swscale- zlib1"
for fname in $ffdlls; do
	cp -v ${WINPREFIX}/bin/${fname}*.dll $NSIDIR
done
cp -v ${WINPREFIX}/bin/ffmpeg.exe $NSIDIR
cp -v ${WINPREFIX}/bin/ffprobe.exe $NSIDIR

TARDIR=$(mktemp -d)
cd $TARDIR
ln -s $NSIDIR harvid
tar cJhf /tmp/harvid_$WARCH-$VERSION.tar.xz harvid
rm -rf $TARDIR
cd -

if test "$WARCH" = "w64"; then
	PGF=PROGRAMFILES64
	SFX=
else
	PGF=PROGRAMFILES
	# TODO we should only add this for 32bit on 64bit windows!
	SFX=" (x86)"
fi

sed "s/@VERSION@/$VERSION/;s/@WARCH@/$WARCH/;s/@PROGRAMFILES@/$PGF/;s/@SFX@/$SFX/" \
	pkg/win/harvid.nsi \
	> $NSIDIR/harvid.nsi

echo
echo "makensis $NSIDIR/harvid.nsi"
makensis "$NSIDIR/harvid.nsi"

echo "--- DONE ---"
cp -v "$NSIDIR/harvid_installer-$WARCH-$VERSION.exe" /tmp/
ls -lt "/tmp/harvid_installer-$WARCH-$VERSION.exe" | head -n 1
ls -lt "/tmp/harvid_$WARCH-$VERSION.tar.xz" | head -n 1
