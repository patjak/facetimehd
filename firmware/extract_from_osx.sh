#!/bin/bash
IN=AppleCameraInterface
OUT=firmware.bin

OSX_HASH=d1db66d71475687a5873dab10a345e2d
FW_HASH=4e1d11e205e5c55d128efa0029b268fe
HASH=$(md5sum $IN | awk '{ print $1 }')

OFFSET=81920
SIZE=603715

if [ "$OSX_HASH" != "$HASH" ]
then
	echo -e "Mismatching driver hash for $IN ($HASH)"
	echo -e "No firmware extracted!"
	exit 1
fi

echo -e "Found matching hash ($HASH)"

dd bs=1 skip=$OFFSET count=$SIZE if=$IN of=$OUT.gz &> /dev/null
gunzip $OUT.gz

RESULT=$(md5sum $OUT | awk '{ print $1 }')

if [ "$RESULT" != "$FW_HASH" ]
then
	echo -e "Firmware hash mismatch ($RESULT)"
	echo -e "No firmware extracted!"
	exit 1;
fi

echo -e "Firmware successfully extracted ($RESULT)"

exit 0
