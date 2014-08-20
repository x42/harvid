#!/bin/sh
## cross-compile and package win32 version of harvid ##

# requires mingw and a working wine install with NSIS
# and dependent libraries installed in wine C:\x-prefix\
# x-compiled ffmpeg, libpng, pthread,.. with
# 'configure prefix=~/.wine/drive_c/x-prefix host=i586-mingw32msvc-gcc ...'

#environment variables
: ${WINPREFIX=$HOME/.wine/drive_c/x-prefix}
: ${WINLIB=${WINPREFIX}/lib}
: ${NSISEXE=$HOME/.wine/drive_c/Program\ Files/NSIS/makensis.exe}

set -e

VERSION=$(git describe --tags HEAD || echo "X.X.X")

if test -z "$NSIDIR"; then
	NSIDIR=$(mktemp -d)
	trap 'rm -rf $NSIDIR' exit
else
	mkdir -p "$NSIDIR"
fi

make ARCH=mingw WINPREFIX=${WINPREFIX} clean
make ARCH=mingw WINPREFIX=${WINPREFIX} CFLAGS="-DNDEBUG -O2"

test -n "$1" && exit 0

# NSI package

cp -v src/harvid $NSIDIR/harvid.exe
i686-w64-mingw32-strip $NSIDIR/harvid.exe

ffdlls="avcodec-55 avdevice-55 avfilter-4 avformat-55 avutil-52 libcharset-1 libiconv-2 libjpeg-9 libmp3lame-0 libogg-0 libpng16-16 libtheora-0 libtheoradec-1 libtheoraenc-1 libvorbis-0 libvorbisenc-2 libvorbisfile-3 libx264-142 postproc-52 pthreadGC2 swresample-0 swscale-2 zlib1"
for fname in $ffdlls; do
	cp -v ${WINPREFIX}/bin/${fname}.dll $NSIDIR
done
cp -v ${WINPREFIX}/bin/ffmpeg.exe $NSIDIR
cp -v ${WINPREFIX}/bin/ffprobe.exe $NSIDIR


sed 's/@VERSION@/'${VERSION}'/' pkg/win/harvid.nsi > $NSIDIR/harvid.nsi

#XXX winepath should probably be moved into a wrapper script "$NSISEXE"
WP=`winepath -w "$NSIDIR/harvid.nsi"`
echo
echo "$NSISEXE" "$WP"
"$NSISEXE" "$WP"

echo "--- DONE ---"
cp -v "$NSIDIR/harvid_installer-$VERSION.exe" /tmp/
ls -lt "/tmp/harvid_installer-$VERSION.exe" | head -n 1
