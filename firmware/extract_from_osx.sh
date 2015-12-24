#!/bin/bash
IN=AppleCameraInterface
OUT=firmware.bin

type md5sum > /dev/null 2>&1 && MD5="md5sum"
type md5 > /dev/null 2>&1 && MD5="md5 -r"

OSX_HASH=d1db66d71475687a5873dab10a345e2d
FW_HASH=4e1d11e205e5c55d128efa0029b268fe
HASH=$($MD5 $IN | awk '{ print $1 }')

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

RESULT=$($CMD $OUT | awk '{ print $1 }')

if [ "$RESULT" != "$FW_HASH" ]
then
	echo -e "Firmware hash mismatch ($RESULT)"
	echo -e "No firmware extracted!"
	exit 1;
fi

echo -e "Firmware successfully extracted ($RESULT)"

exit 0
