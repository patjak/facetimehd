#! /bin/bash
# Some inspiration from the AUR pkgbuild

if [ ! whoami = root ]; then
  echo 'You must be root to run this script.'
  exit 13
fi

pkgname=bcwc-pcie
pkgver=r245.82626d4
url="https://github.com/patjak/bcwc_pcie"
fwurl="https://github.com/patjak/facetimehd-firmware"
fwname=facetimehd-firmware
srcdir=src

rm -rf ${srcdir}

mkdir $srcdir

if [ ! -f /usr/lib/firmware/facetimehd/firmware.bin ]; then
  echo 'WARNING: Requires facetimehd-firmware, but it was not found. Installing it for you...'
  (cd src; git clone $fwurl $fwname)
  pushd ${srcdir}/${fwname} > /dev/null
  make
  mkdir /usr/lib/firmware/facetimehd/
  install -Dm 644 firmware.bin /usr/lib/firmware/facetimehd/firmware.bin
  popd > /dev/null
fi

(cd $srcdir; git clone $url $pkgname)

pushd $srcdir/$pkgname/ > /dev/null
for FILE in dkms.conf Makefile *.[ch]; do
  install -Dm 644 "$FILE" "/usr/src/${pkgname}-${pkgver}/$FILE"
done
popd > /dev/null

echo 'Creating modprobe config files'
echo 'facetimehd' > /etc/modules-load.d/bcwc-pcie.conf
echo 'blacklist bdc_pci' > /etc/modprobe.d/bcwc-pcie.conf

echo 'DKMS install for current kernel'
dkms add $pkgname -v $pkgver; dkms install $pkgname -v $pkgver
