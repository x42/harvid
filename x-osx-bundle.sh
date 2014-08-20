#!/bin/bash

set -e

: ${HVSTACK=$HOME/hvstack}
: ${HVARCH=-arch i386 -arch ppc -arch x86_64}
: ${OSXCOMPAT="-isysroot /Developer/SDKs/MacOSX10.5.sdk -mmacosx-version-min=10.5 -headerpad_max_install_names"}

TARGET=/tmp/
VERSION=$(git describe --tags HEAD || echo "X.X.X")

TOPDIR=$(pwd)
export PKG_CONFIG_PATH=${HVSTACK}/lib/pkgconfig
make clean
make CFLAGS="-DNDEBUG -O2 ${HVARCH} ${OSXCOMPAT}"

file src/harvid

PREFIX=`mktemp -d -t harvid-pkg`
trap "rm -rf ${PREFIX}" EXIT

rm -rf $PREFIX
cp -a pkg/osx/ $PREFIX

TDIR=$PREFIX/usr/local/bin
LDIR=$PREFIX/usr/local/lib/harvid

mkdir -p $TDIR
mkdir -p $LDIR

#############################################################################
# DEPLOY
cp -fv src/harvid "$TDIR/harvid"
strip -SXx "$TDIR/harvid"

cp -fv "$HVSTACK/bin/ffprobe" "$TDIR/ffprobe_harvid"
strip -SXx "$TDIR/ffprobe_harvid"

cp -fv "$HVSTACK/bin/ffmpeg" "$TDIR/ffmpeg_harvid"
strip -SXx "$TDIR/ffmpeg_harvid"

mkdir -p $PREFIX/usr/local/man/man1/
cp -fv "${TOPDIR}/doc/harvid.1" $PREFIX/usr/local/man/man1/
cp -fv "${HVSTACK}/share/man/man1/ffmpeg.1" $PREFIX/usr/local/man/man1/ffmpeg_harvid.1
cp -fv "${HVSTACK}/share/man/man1/ffprobe.1" $PREFIX/usr/local/man/man1/ffprobe_harvid.1

echo

##############################################################################
# add dependancies..

echo "bundle libraries ..."
while [ true ] ; do
	missing=false
	for file in ${TDIR}/* ${LDIR}/*; do
		set +e # grep may return 1
		if ! file $file | grep -qs Mach-O ; then
			continue;
		fi
		deps=`otool -arch all -L $file \
			| awk '{print $1}' \
			| egrep "($HVSTACK|/opt/|/local/|libs/)" \
			| grep -v '/bin/'`
		set -e
		for dep in $deps ; do
			base=`basename $dep`
			if ! test -f ${LDIR}/$base; then
				cp -v $dep ${LDIR}/
			  missing=true
			fi
		done
	done
	if test x$missing = xfalse ; then
		break
	fi
done

echo "update executables ..."
for exe in ${TDIR}/*; do
	set +e # grep may return 1
	if ! file $exe | grep -qs Mach-O ; then
		continue
	fi
	changes=""
	libs=`otool -arch all -L $exe \
		| awk '{print $1}' \
		| egrep "($HVSTACK|/opt/|/local/|libs/)" \
		| grep -v '/bin/'`
	set -e
	for lib in $libs; do
		base=`basename $lib`
		changes="$changes -change $lib @executable_path/../lib/harvid/$base"
	done
	if test "x$changes" != "x" ; then
		install_name_tool $changes $exe
	fi
done

echo "update libraries ..."
for dylib in ${LDIR}/*.dylib ; do
	# skip symlinks
	if test -L $dylib ; then
		continue
	fi
	strip -SXx $dylib

	# change all the dependencies
	changes=""
	libs=`otool -arch all -L $dylib \
		| awk '{print $1}' \
		| egrep "($HVSTACK|/opt/|/local/|libs/)" \
		| grep -v '/bin/'`
	for lib in $libs; do
		base=`basename $lib`
		changes="$changes -change $lib @executable_path/../lib/harvid/$base"
	done

	if test "x$changes" != x ; then
		if  install_name_tool $changes $dylib ; then
			:
		else
			exit 1
		fi
	fi

	# now the change what the library thinks its own name is
	base=`basename $dylib`
	install_name_tool -id @executable_path/../lib/harvid/$base $dylib
done

echo "all bundled up."

##############################################################################
# package & DMG

echo "------- Build Package"
cd "$TOPDIR"
test -d $PREFIX/Resources/harvid.pmdoc

mkdir -p "${TARGET}"

SHORTVS=$(echo $VERSION | sed 's/^v\([0-9.]*\).*$/\1/')
echo "calling packagemaker"
/Developer/usr/bin/packagemaker \
	-d $PREFIX/Resources/harvid.pmdoc \
	-v --id gareus.org.sodankyla.harvid.pkg \
	--out "${TARGET}/harvid-${VERSION}.pkg" \
	--version $SHORTVS \
	--title "harvid"

ls -l "${TARGET}/harvid-${VERSION}.pkg"

echo
echo "------- Preparing bundle"
echo

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

echo
echo "------- Building DMG"
echo

UC_DMG="${TARGET}/$APPNAME-${VERSION}.dmg"
VOLNAME=$APPNAME-${VERSION}

MNTPATH=`mktemp -d -t harvidimg`
TMPDMG=`mktemp -t harvid`
ICNSTMP=`mktemp -t harvidicon`
DMGSIZE=$[ `du -sm "$PREFIX" | cut -f 1` * 1049 / 1000 + 3 ]

trap "rm -rf $MNTPATH $TMPDMG ${TMPDMG}.dmg $ICNSTMP ${PREFIX}" EXIT

rm -f "$UC_DMG" "$TMPDMG" "${TMPDMG}.dmg" "$ICNSTMP"
rm -rf "$MNTPATH"
mkdir -p "$MNTPATH"

TMPDMG="${TMPDMG}.dmg"

hdiutil create -megabytes $DMGSIZE "$TMPDMG"
DiskDevice=$(hdid -nomount "$TMPDMG" | grep Apple_HFS | cut -f 1 -d ' ')
newfs_hfs -v "${VOLNAME}" "${DiskDevice}"
mount -t hfs "${DiskDevice}" "${MNTPATH}"

cp -r ${PREFIX}/${APPDIR} "${MNTPATH}"
mkdir "${MNTPATH}/.background"
cp -vi "${TOPDIR}/doc/dmgbg.png" "${MNTPATH}/.background/dmgbg.png"

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
' | osascript || {
	umount "${DiskDevice}"
	hdiutil eject "${DiskDevice}"
	exit 1
}

set +e
chmod -Rf go-w "${MNTPATH}"
set -e
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

cd ${PREFIX}/${APPDIR}/Contents/
tar czf ${TARGET}/harvid-osx-${VERSION}.tgz \
	--exclude  MacOS/harvid_param \
	MacOS lib/harvid

rm -rf ${PREFIX}

echo "Done."
