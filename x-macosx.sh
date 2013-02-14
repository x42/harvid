#!/bin/bash
## create a OSX package with harvid and ffmpeg ##

# only runs on OSX and requires a universal ffmpeg install and sourcecode.
#
# ffmpeg can not cleanly be compiled as universal binary/lib. So 3 checkouts
# of of the sourcecode are used:
#  ~/src/ffmpeg-git-i386
#  ~/src/ffmpeg-git-x86_64
#  ~/src/ffmpeg-git-ppc
# the resulting libs are combined with
#  lipo -create -output /usr/local/lib/${OF} ${FFSOURCE}ffmpeg-git*/$1/$1.[0-9]*.dylib
#  ...
# the ffmpeg, ffprobe universal executable are created by this script and
# included in the package along with libs from /usr/local/

FFSOURCE=$HOME/src/
VERSION=$(git describe --tags HEAD || echo "X.X.X")

make clean
make CFLAGS="-DNDEBUG -O2"

test -f src/harvid || exit
file src/harvid | grep "binary with 3 architectures" &> /dev/null || exit

TOPDIR=$(pwd)
export PREFIX=/tmp/harvid-pkg

rm -rf $PREFIX
cp -a pkg/osx/ $PREFIX

export TDIR=$PREFIX/usr/local/bin
export LREL=../lib/harvid
export LDIR=$TDIR/$LREL
export LPRE="@executable_path/$LREL"
export INSTALLED=""

mkdir -p $TDIR
mkdir -p $LDIR

follow_dependencies () {
    libname=$1
    cd "$LDIR"
    #echo "follow $libname"
    dependencies=`otool -arch all -L "$libname"  | egrep '^[^\/]*\/usr\/local\/lib' | awk '{print $1}'`
    for l in $dependencies; do
        #echo "..following $l:"
        depname=`basename $l`
        deppath=`dirname $l`
        if [ ! -f "$LDIR/$depname" ]; then
            deploy_lib $depname "$deppath"
        fi
    done
}

update_links () {
    libname=$1
    libpath=$2
    for n in `ls $LDIR/*`; do
        install_name_tool \
            -change "$libpath/$libname" \
            $LPRE/$libname \
            "$n"
    done
}

deploy_lib () {
    libname=$1
    libpath=$2
    check=`echo $INSTALLED | grep $libname`
    if [ "X$check" = "X" ]; then
        if [ ! -f "$LDIR/$libname" ]; then
            echo "installing: $libname"
            cp -f "$libpath/$libname" "$LDIR/$libname"
            install_name_tool \
                -id $LPRE/$libname \
                "$LDIR/$libname"
            follow_dependencies $libname
        fi
        export INSTALLED="$INSTALLED $libname"
    fi
    update_links $libname $libpath
}

update_executable() {
    LIBS=`otool -arch all -L "$TARGET" | egrep '^[^\/]*\/usr\/local\/lib' | awk '{print $1}'`
    for l in $LIBS; do
        libname=`basename $l`
        libpath=`dirname $l`
        deploy_lib $libname $libpath
        install_name_tool \
            -change $libpath/$libname \
            $LPRE/$libname \
            "$TARGET"
    done
}

echo "------- DEPLOY"
export TARGET=$TDIR/harvid
cp -f src/harvid "$TARGET"
strip "$TARGET"
update_executable
update_executable
file "$TARGET"
otool -arch all -L "$TARGET"

echo "-------"
export TARGET="$TDIR/ffprobe_harvid"
lipo -create -o "$TARGET" ${FFSOURCE}ffmpeg-git*/ffprobe
strip "$TARGET"
update_executable
update_executable
file "$TARGET"
otool -arch all -L "$TARGET"

echo "-------"
export TARGET="$TDIR/ffmpeg_harvid"
lipo -create -o "$TARGET" ${FFSOURCE}ffmpeg-git*/ffmpeg
strip "$TARGET"
update_executable
update_executable
file "$TARGET"
otool -arch all -L "$TARGET"

echo "-------"
cd $LDIR && MORELIBS=`otool -arch all -L * | egrep '^[^\/]*\/usr\/local\/lib' | awk '{print $1}'` && cd - > /dev/null
while [ "X$MORELIBS" != "X" ]; do
    for l in $MORELIBS; do
        libname=`basename $l`
        libpath=`dirname $l`
        deploy_lib "$libname" "$libpath"
    done
    cd $LDIR && MORELIBS=`otool -arch all -L * | egrep '^[^\/]*\/(opt|usr)\/local\/lib' | awk '{print $1}'` && cd - > /dev/null
done

#otool -arch all -L $LDIR/*.dylib


echo "------- Install manual pages"
cd "$TOPDIR"
mkdir -p $PREFIX/usr/local/man/man1/
cp doc/harvid.1 $PREFIX/usr/local/man/man1/
cp ${FFSOURCE}ffmpeg-git-ppc/doc/ffmpeg.1 $PREFIX/usr/local/man/man1/ffmpeg_harvid.1
cp ${FFSOURCE}ffmpeg-git-ppc/doc/ffprobe.1 $PREFIX/usr/local/man/man1/ffprobe_harvid.1

echo "------- BUILD PACKAGE"
cd "$TOPDIR"
test -d $PREFIX/Resources/harvid.pmdoc || exit 1

SHORTVS=$(echo $VERSION | sed 's/^v\([0-9.]*\).*$/\1/')
echo "calling packagemaker"
/Developer/usr/bin/packagemaker \
	-d $PREFIX/Resources/harvid.pmdoc \
	-v --id gareus.org.sodankyla.harvid.pkg \
	--out ~/Desktop/harvid-${VERSION}.pkg \
	--version $SHORTVS \
	--title "harvid"

ls -l ~/Desktop/harvid-${VERSION}.pkg
