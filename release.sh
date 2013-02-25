#!/bin/bash

#environment variables
: ${OSXMACHINE:=priroda.local}

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

	dch --newversion "${NEWVERSION}-1" --distribution unstable || exit
	make VERSION="v${NEWVERSION}" clean man || exit

	git status -s
	echo " - Version v${NEWVERSION}"

	echo -n "git commit and tag? [Y/n]"
	read -n1 a
	echo
	if test "$a" == "n" -o "$a" == "N"; then
		exit 1
	fi

	git commit -a -m "finalize changelog"
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

echo "building win32 ..."
./x-win32.sh || exit
echo "building linux static ..."
./x-static.sh || exit
echo "building osx package on $OSXMACHINE ..."

ssh $OSXMACHINE << EOF
exec /bin/bash -l
cd src/harvid || exit 1
git pull || exit 1
./x-macosx.sh
EOF

ok=$?
if test "$ok" != 0; then
	echo "remote build failed"
	exit
fi

rsync -Pa $OSXMACHINE:Desktop/mydmg/harvid-${VERSION}.pkg site/releases/ || exit

echo "preparing website"

# git clone --single-branch -b gh-pages site

sed 's/@VERSION@/'$VERSION'/g;s/@DATE@/'"`date -R`"'/g;' site/index.tpl.html > site/index.html || exit
groff -m mandoc -Thtml doc/harvid.1 > site/harvid.1.html


cd site || exit
git add harvid.1.html
git add releases/*-${VERSION}.* || exit
git rm -f $(ls releases/* | grep -v ${VERSION} | tr '\n' ' ')
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

git push --force
