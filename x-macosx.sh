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

test -f src/harvid || exit 1
file src/harvid | grep --silent "binary with 3 architectures" || exit 1

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

echo "------- Deploy binaries"
echo " * harvid"
export TARGET=$TDIR/harvid
cp -fv src/harvid "$TARGET" || exit 1
strip "$TARGET"
update_executable
update_executable
file "$TARGET" | grep --silent "binary with 3 architectures" || exit 1
otool -arch all -L "$TARGET"

echo " * ffprobe"
export TARGET="$TDIR/ffprobe_harvid"
lipo -create -o "$TARGET" ${FFSOURCE}ffmpeg-git*/ffprobe
strip "$TARGET"
update_executable
update_executable
file "$TARGET" | grep --silent "binary with 3 architectures" || exit 1
otool -arch all -L "$TARGET"

echo " * ffmpeg"
export TARGET="$TDIR/ffmpeg_harvid"
lipo -create -o "$TARGET" ${FFSOURCE}ffmpeg-git*/ffmpeg
strip "$TARGET"
update_executable
update_executable
file "$TARGET" | grep --silent "binary with 3 architectures" || exit 1
otool -arch all -L "$TARGET"

echo "------- Follow library dependencies"
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

###############################################################################


echo "------- Install manual pages"
cd "$TOPDIR"
mkdir -p $PREFIX/usr/local/man/man1/
cp doc/harvid.1 $PREFIX/usr/local/man/man1/
cp ${FFSOURCE}ffmpeg-git-ppc/doc/ffmpeg.1 $PREFIX/usr/local/man/man1/ffmpeg_harvid.1
cp ${FFSOURCE}ffmpeg-git-ppc/doc/ffprobe.1 $PREFIX/usr/local/man/man1/ffprobe_harvid.1

echo "------- Build Package"
cd "$TOPDIR"
test -d $PREFIX/Resources/harvid.pmdoc || exit 1

mkdir -p ~/Desktop/mydmg/

SHORTVS=$(echo $VERSION | sed 's/^v\([0-9.]*\).*$/\1/')
echo "calling packagemaker"
/Developer/usr/bin/packagemaker \
	-d $PREFIX/Resources/harvid.pmdoc \
	-v --id gareus.org.sodankyla.harvid.pkg \
	--out ~/Desktop/mydmg/harvid-${VERSION}.pkg \
	--version $SHORTVS \
	--title "harvid"

ls -l ~/Desktop/mydmg/harvid-${VERSION}.pkg

echo "------- Preparing bundle"
APPNAME=Harvid
APPDIR=${APPNAME}.app

mv $PREFIX/usr $PREFIX/${APPDIR}
mv $PREFIX/${APPDIR}/local $PREFIX/${APPDIR}/Contents
mv $PREFIX/${APPDIR}/Contents/bin $PREFIX/${APPDIR}/Contents/MacOS
rm -rf $PREFIX/Resources/
mkdir $PREFIX/${APPDIR}/Contents/Resources

cp pkg/osx/Resources/harvid.icns $PREFIX/${APPDIR}/Contents/Resources/

cat > $PREFIX/${APPDIR}/Contents/MacOS/harvid_param << EOF
#!/usr/bin/env bash
CWD="\`/usr/bin/dirname \"\$0\"\`"

/usr/bin/osascript -e '
  tell application "Finder"
    display dialog "harvid is a system service without graphical user interface. Visit http://localhost:1554 to interact with it." buttons["OK"]
  end tell'

(sleep 3; open http://localhost:1554) &
exec "\$CWD/harvid" -A shutdown
EOF

chmod +x $PREFIX/${APPDIR}/Contents/MacOS/harvid_param

cat > $PREFIX/${APPDIR}/Contents/Info.plist << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleExecutable</key>
	<string>harvid_param</string>
	<key>CFBundleName</key>
	<string>Harvid</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleSignature</key>
	<string>~~~~</string>
	<key>CFBundleVersion</key>
	<string>1.0</string>
	<key>CFBundleIconFile</key>
	<string>harvid</string>
  <key>CFBundleIdentifier</key>
  <string>org.gareus.harvid</string>
	<key>CSResourcesFileMapped</key>
	<true/>
	<key>LSUIElement</key>
	<string>1</string>
</dict>
</plist>
EOF

ls -l $PREFIX/

echo "------- Building DMG"

UC_DMG=~/Desktop/mydmg/$APPNAME-${VERSION}.dmg
VOLNAME=$APPNAME-${VERSION}

MNTPATH=`mktemp -d -t harvidimg`
TMPDMG=`mktemp -t harvid`
ICNSTMP=`mktemp -t harvidicon`
DMGSIZE=$[ `du -sm "$PREFIX" | cut -f 1` * 1049 / 1000 + 3 ]

rm -f $UC_DMG "$TMPDMG" "${TMPDMG}.dmg" "$ICNSTMP"
rm -rf "$MNTPATH"
mkdir -p "$MNTPATH"

TMPDMG="${TMPDMG}.dmg"

hdiutil create -megabytes $DMGSIZE "$TMPDMG"
DiskDevice=$(hdid -nomount "$TMPDMG" | grep Apple_HFS | cut -f 1 -d ' ')
newfs_hfs -v "${VOLNAME}" "${DiskDevice}"
mount -t hfs "${DiskDevice}" "${MNTPATH}"

cp -r ${PREFIX}/${APPDIR} "${MNTPATH}" || exit
mkdir "${MNTPATH}/.background"
cp -vi doc/dmgbg.png "${MNTPATH}/.background/dmgbg.png"

echo "setting DMG background ..."

echo '
   tell application "Finder"
     tell disk "'${VOLNAME}'"
           open
           set current view of container window to icon view
           set toolbar visible of container window to false
           set statusbar visible of container window to false
           set the bounds of container window to {400, 200, 800, 440}
           set theViewOptions to the icon view options of container window
           set arrangement of theViewOptions to not arranged
           set icon size of theViewOptions to 64
           set background picture of theViewOptions to file ".background:dmgbg.png"
           make new alias file at container window to POSIX file "/Applications" with properties {name:"Applications"}
           set position of item "'${APPDIR}'" of container window to {90, 100}
           set position of item "Applications" of container window to {310, 100}
           close
           open
           update without registering applications
           delay 5
           eject
     end tell
   end tell
' | osascript

chmod -Rf go-w "${MNTPATH}"
sync

echo "compressing Image ..."

# Umount the image
umount "${DiskDevice}"
hdiutil eject "${DiskDevice}"
# Create a read-only version, use zlib compression
hdiutil convert -format UDZO "${TMPDMG}" -imagekey zlib-level=9 -o "${UC_DMG}"
# Delete the temporary files
rm "$TMPDMG"
rmdir "$MNTPATH"

echo "setting file icon ..."

cp pkg/osx/Resources/harvid.icns ${ICNSTMP}.icns
/usr/bin/sips -i ${ICNSTMP}.icns
/Developer/Tools/DeRez -only icns ${ICNSTMP}.icns > ${ICNSTMP}.rsrc
/Developer/Tools/Rez -append ${ICNSTMP}.rsrc -o "$UC_DMG"
/Developer/Tools/SetFile -a C "$UC_DMG"

rm -f ${ICNSTMP}.icns ${ICNSTMP}.rsrc

echo
echo "DMG packaging suceeded."
ls -l "$UC_DMG"

echo
echo "rolling .tgz"

cd $PREFIX/${APPDIR}/Contents/
tar czf ~/Desktop/mydmg/havid-${VERSION}.tgz \
	--exclude  MacOS/harvid_param \
	MacOS lib/harvid

echo "Done."

exit



###############################################################################
# copy binary to git-pages
: ${DEV_HOSTNAME:="soyuz.local"}
/sbin/ping -q -c1 ${DEV_HOSTNAME} &>/dev/null \
	  && /usr/sbin/arp -n ${DEV_HOSTNAME} &>/dev/null
ok=$?
if test "$ok" != 0; then
	exit
fi

scp ~/Desktop/mydmg/harvid-${VERSION}.pkg ${DEV_HOSTNAME}:data/coding/harvid/site/releases/
