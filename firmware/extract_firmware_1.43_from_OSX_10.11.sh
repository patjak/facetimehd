#!/bin/bash
IN=/System/Library/Extensions/AppleCameraInterface.kext/Contents/MacOS/AppleCameraInterface
OUT=firmware.bin

OSX_HASH=ccea5db116954513252db1ccb639ce95
FW_HASH=4e1d11e205e5c55d128efa0029b268fe
HASH=$(md5 -r $IN | awk '{ print $1 }')

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

RESULT=$(md5 -r $OUT | awk '{ print $1 }')

if [ "$RESULT" != "$FW_HASH" ]
then
	echo -e "Firmware hash mismatch ($RESULT)"
	echo -e "No firmware extracted!"
	exit 1;
fi

echo -e "Firmware successfully extracted ($RESULT)"

exit 0
