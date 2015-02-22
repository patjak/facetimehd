#!/bin/bash -e

IN=/System/Library/Extensions/AppleCameraInterface.kext/Contents/MacOS/AppleCameraInterface

gcc decompress.c -o decompress -lz -Wall
segedit "$IN" -extract __DATA S2ISPFIRMWARE firmware.raw
#objcopy -O binary --only-section=__DATA.S2ISPFIRMWARE "$IN" firmware.raw
./decompress firmware.raw firmware.bin
