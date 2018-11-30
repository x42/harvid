#!/bin/bash

#environment variables
: ${OSXUSER:=}
: ${OSXMACHINE:=cowbuilder.local}
: ${COWBUILDER:=osxbuilder.local}
test -f "$HOME/.buildcfg.sh" && . "$HOME/.buildcfg.sh"

NEWVERSION=$1

if test -n "$(echo "$NEWVERSION" | sed 's/^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$//')"; then
	NEWVERSION=""
fi

if test -z "$NEWVERSION"; then
	echo -n "No X.X.X version given as argument. Do a binary build only? [y/N] "
	read -n1 a
	echo
	if test "$a" != "y" -a "$a" != "Y"; then
		exit 1
	fi
fi

if test -n "$NEWVERSION"; then

	echo "commit pending changes.."
	git commit -a

	dch --newversion "${NEWVERSION}-0.1" --distribution unstable || exit
	vi ChangeLog
	make VERSION="v${NEWVERSION}" clean man || exit

	git status -s
	echo " - Version v${NEWVERSION}"

	echo -n "git commit and tag? [Y/n]"
	read -n1 a
	echo
	if test "$a" == "n" -o "$a" == "N"; then
		exit 1
	fi

	git commit -m "finalize changelog" debian/changelog ChangeLog doc/harvid.1
	git tag "v${NEWVERSION}"

	echo -n "git push and build? [Y/n] "
	read -n1 a
	echo
	if test "$a" == "n" -o "$a" == "N"; then
		exit 1
	fi

	git push origin && git push origin --tags
	git push rg42 && git push rg42 --tags

fi

VERSION=$(git describe --tags HEAD)
test -n "$VERSION" || exit


/bin/ping -q -c1 ${OSXMACHINE} &>/dev/null \
	&& /usr/sbin/arp -n ${OSXMACHINE} &>/dev/null
ok=$?
if test "$ok" != 0; then
	echo "OSX build host can not be reached."
	exit
fi

/bin/ping -q -c1 ${COWBUILDER} &>/dev/null \
	&& /usr/sbin/arp -n ${COWBUILDER} &>/dev/null
ok=$?
if test "$ok" != 0; then
	echo "Linux cowbuild host can not be reached."
	exit
fi

echo "building linux static and windows versions"
ssh $COWBUILDER ~/bin/build-harvid.sh

ok=$?
if test "$ok" != 0; then
	echo "remote build failed"
	exit
fi

if test -n "$OSXFROMSCRATCH"; then
  echo "building osx package from scratch"
  ssh ${OSXUSER}${OSXMACHINE} << EOF
exec /bin/bash -l
curl -L -o /tmp/harvid-x-pbuildstatic.sh https://raw.github.com/x42/harvid/master/x-osx-buildstack.sh
chmod +x /tmp/harvid-x-pbuildstatic.sh
/tmp/harvid-x-pbuildstatic.sh
EOF
else
  echo "building osx package with existing stack"
  ssh ${OSXUSER}${OSXMACHINE} << EOF
exec /bin/bash -l
rm -rf harvid
git clone -b master --single-branch git://github.com/x42/harvid.git
cd harvid
./x-osx-bundle.sh
cd ..
rm -rf harvid
EOF
fi


ok=$?
if test "$ok" != 0; then
	echo "remote build failed"
	exit
fi


rsync -Pa $COWBUILDER:/tmp/harvid-i386-linux-gnu-${VERSION}.tgz site/releases/ || exit
rsync -Pa $COWBUILDER:/tmp/harvid-x86_64-linux-gnu-${VERSION}.tgz site/releases/ || exit
rsync -Pa $COWBUILDER:/tmp/harvid_installer-w32-${VERSION}.exe site/releases/ || exit
rsync -Pa $COWBUILDER:/tmp/harvid_installer-w64-${VERSION}.exe site/releases/ || exit
rsync -Pa $COWBUILDER:/tmp/harvid_w32-${VERSION}.tar.xz tmp/ || exit
rsync -Pa $COWBUILDER:/tmp/harvid_w64-${VERSION}.tar.xz tmp/ || exit

rsync -Pa ${OSXUSER}$OSXMACHINE:/tmp/harvid-${VERSION}.pkg site/releases/ || exit
rsync -Pa ${OSXUSER}$OSXMACHINE:/tmp/harvid-${VERSION}.dmg site/releases/ || exit
rsync -Pa ${OSXUSER}$OSXMACHINE:/tmp/harvid-osx-${VERSION}.tgz tmp/ || exit

echo -n "${VERSION}" > site/releases/harvid_version.txt

echo "preparing website"

# git clone --single-branch -b gh-pages site

sed 's/@VERSION@/'$VERSION'/g;s/@DATE@/'"`date -R`"'/g;' site/index.tpl.html > site/index.html || exit
groff -m mandoc -Thtml doc/harvid.1 > site/harvid.1.html


cd site || exit
git add harvid.1.html releases/harvid_version.txt
git add releases/*-${VERSION}.* || exit
rm -f $(ls releases/* | grep -v "${VERSION}\." | grep -v harvid_version.txt | grep -v harvid-v0.8.2.dmg | grep -v harvid-v0.8.2.pkg | tr '\n' ' ')
git commit -a --amend -m "website $VERSION" || exit
git reflog expire --expire=now --all
git gc --prune=now
git gc --aggressive --prune=now


echo -n "git upload site and binaries? [Y/n] "
read -n1 a
echo
if test "$a" == "n" -o "$a" == "N"; then
	exit 1
fi

echo "uploading to github.."
git push --force

echo "uploading to ardour.org"
rsync -Pa \
	../tmp/harvid-osx-${VERSION}.tgz \
	../tmp/harvid_w32-${VERSION}.tar.xz \
	../tmp/harvid_w64-${VERSION}.tar.xz \
	releases/harvid-${VERSION}.dmg \
	releases/harvid-${VERSION}.pkg \
	releases/harvid-i386-linux-gnu-${VERSION}.tgz \
	releases/harvid-x86_64-linux-gnu-${VERSION}.tgz  \
	releases/harvid_installer-w32-${VERSION}.exe \
	releases/harvid_installer-w64-${VERSION}.exe \
	releases/harvid_version.txt \
		ardour.org:/persist/community.ardour.org/files/video-tools/
