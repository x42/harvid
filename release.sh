#!/bin/bash

NEWVERSION=$1
OSXMACHINE=priroda.local

test -n "$NEWVERSION" || exit
test -z "$(echo "$NEWVERSION" | sed 's/^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$//')" || exit
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


echo -n "git push and build? [Y/n]"
read -n1 a
echo
if test "$a" == "n" -o "$a" == "N"; then
	exit 1
fi

git push origin && git push origin --tags
git push rg42 && git push rg42 --tags

VERSION=$(git describe --tags HEAD)
test -n "$VERSION" || exit

./x-win32.sh || exit
./x-static.sh || exit
ssh $OSXMACHINE << EOF
exec /bin/bash -l
cd src/harvid
git pull
./x-macosx.sh
EOF

rsync -Pa $OSXMACHINE:Desktop/mydmg/harvid-${VERSION}.pkg site/releases/ || exit

sed 's/@VERSION@/'$VERSION'/g;s/@DATE@/'"`date -R`"'/g;' site/index.tpl.html > site/index.html || exit

cd site || exit
git add releases/*${VERSION}*
git commit -a -m "release $VERSION" || exit
git push
