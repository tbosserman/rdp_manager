#!/bin/sh

set -- $(grep VERSION version.h)
eval RDP_VERSION=$3

DPKG_VERSION=$(cat DPKG_VERSION)

ARCH=$(arch)
case $(arch) in
    x86_64) ARCH=amd64;;
    aarch64) ARCH=arm64;;
    *)
	echo "Unrecognized architecture"
	exit 1;;
esac

DIR="rdp_manager_${RDP_VERSION}-${DPKG_VERSION}_$ARCH"
echo "RDP_VERSION=$RDP_VERSION"
echo "DPKG_VERSION=$DPKG_VERSION"
echo "ARCH=$ARCH"
echo "DIR=$DIR"

DIRLIST="
$DIR
$DIR/DEBIAN
$DIR/etc
$DIR/etc/NetworkManager
$DIR/etc/NetworkManager/dispatcher.d
$DIR/usr
$DIR/usr/local
$DIR/usr/local/bin
$DIR/usr/local/share
$DIR/usr/local/share/images
$DIR/usr/share
$DIR/usr/share/applications
"

rm -rf $DIR
for DIRNAME in $DIRLIST
do
    echo "Making directory $DIRNAME"
    mkdir $DIRNAME
done

# generate the dpkg control file
cat <<EOF > $DIR/DEBIAN/control
Package: rdp-manager
Version: $RDP_VERSION
Architecture: $ARCH
Maintainer: I.T. Solutions <support@its-ia.com>
Description: An app to manage XfreeRDP connections
Section: Network
Depends: freerdp2-x11,libgtk-3-0,libssl3
EOF

# Put all the files into place.
for FNAME in $(cat DPKG_FILES)
do
    SRC=$(basename $FNAME)
    FROMDIR=dpkg_files
    [ "$SRC" = "rdp_manager" -o "$SRC" = "noip2" ] && FROMDIR=.
    [ "$SRC" = "rdp_manager" ] && SRC="rdp_manager.$ARCH"
    [ "$SRC" = "noip2" ] && SRC="noip2.$ARCH"
    echo cp $FROMDIR/$SRC $DIR/$FNAME
    cp $FROMDIR/$SRC $DIR/$FNAME
done

dpkg-deb --build --root-owner-group $DIR
