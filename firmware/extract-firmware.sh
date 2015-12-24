#!/bin/bash

# Known driver and firmware hashes

# NOTE: use sha256 checksums as they are more robust that md5 against collisions
hash_drv_wnd_105='6ec37d48c0764ed059dd49f472456a4f70150297d6397b7cc7965034cf78627e'
hash_drv_wnd_138='7044344593bfc08ab9b41ab691213bca568c8d924d0e05136b537f66b3c46f31'
hash_drv_osx_143='4667e6828f6bfc690a39cf9d561369a525f44394f48d0a98d750931b2f3f278b'

hash_fw_wnd_105='dabb8cf8e874451ebc85c51ef524bd83ddfa237c9ba2e191f8532b896594e50e'
hash_fw_wnd_138='ed75dc37b1a0e19949e9e046a629cb55deb6eec0f13ba8fd8dd49b5ccd5a800e'
hash_fw_osx_143='e3e6034a67dfdaa27672dd547698bbc5b33f47f1fc7f5572a2fb68ea09d32d3d'

# Driver names
declare -A known_hashes=(
  ["$hash_drv_wnd_105"]='Windows Boot Camp 5.1.5722'
  ["$hash_drv_wnd_138"]='Windows Boot Camp Update Jul 29, 2015'
  ["$hash_drv_osx_143"]='OS X, El Capitan'
)

# Offset in bytes of the firmware inside the driver
declare -A firmw_offsets=(
  ["$hash_drv_wnd_105"]=78208
  ["$hash_drv_wnd_138"]=85296
  ["$hash_drv_osx_143"]=81920
)

# Size in bytes of the firmware inside the driver 
declare -A firmw_sizes=(
  ["$hash_drv_wnd_105"]=1523716
  ["$hash_drv_wnd_138"]=1421316
  ["$hash_drv_osx_143"]=603715
)

# Compression method used to store the firmware inside the driver
declare -A compression=(
  ["$hash_drv_wnd_105"]='cat'
  ["$hash_drv_wnd_138"]='cat'
  ["$hash_drv_osx_143"]='gzip'
)

declare -A firmw_hashes=(
  ["$hash_fw_wnd_105"]='1.05'
  ["$hash_fw_wnd_138"]='1.38'
  ["$hash_fw_osx_143"]='1.43'
)

printHelp()
{
  cat <<HELP_DOC
Usage: extract-firmware [OPTIONS] FILE

OPTION:

  -h  --help          Print this help message and exit.

FILE:

  The url of the file containing the driver. At the moment only two drivers
  are currently available:

   - AppleCameraInterface: this is the OS X native driver, it can be found
     in a OS X installation under the following system directory
     /System/Library/Extensions/AppleCameraInterface.kext/Contents/MacOS/

   - AppleCamera.sys: this comes within the bootcamp windows driver package.
     You can download it from http://support.apple.com/downloads/DL1831/"

HELP_DOC
}

getCheckSum()
{
  sha256sum $1 | awk '{ print $1 }'
}

checkDriverHash()
{
  # computing the hash for the input file
  driver_hash="$(getCheckSum $1)"

  # checking if it is among the known hashes
  for cur_hash in "${!known_hashes[@]}"; do
    if [[ "$driver_hash" == "$cur_hash" ]]; then
      echo "Found matching hash from ${known_hashes[$cur_hash]}"
      return
    fi
  done

  echo "Mismatching driver hash for $1"
  echo "The uknown hash is ${driver_hash}"
	echo "No firmware extracted!"
  exit 1
}

checkFirmwareHash()
{
  # computing the hash for the input file
  fw_hash="$(getCheckSum $1)"

  # checking if it is among the known hashes
  for cur_hash in "${!firmw_hashes[@]}"; do
    if [[ "$fw_hash" == "$cur_hash" ]]; then
      echo "Extracted firmware version ${firmw_hashes[$cur_hash]}"
      return 0
    fi
  done

  echo "Mismatching firmware hash ${firm_hash}"
	echo "No firmware extracted!"
  return 1
}

extractFirmware()
{
  echo "Extracting firmware..."
  dd bs=1 skip=$3 count=$4 if=$1 of="$2.tmp" &> /dev/null

  echo "Decompressing the firmware using $5..."
  case "$5" in
    "gzip")
      zcat "$2.tmp" > "$2"
      ;;
    "cat")
      cat "$2.tmp" > "$2"
      ;;
  esac

  echo "Deleting temporary files..."
  rm "$2.tmp"
}

main()
{
  echo ""
  if [[ -z "$1" ]]; then
    echo "No input file specified!"
    printHelp
    exit 1
  elif [[ "$1" == "--help" ]] || [[ "$1" == "-h" ]]; then
    printHelp
    exit 0
  elif [[ ! -f "$1" ]]; then
    echo "'$1' is not a file or it does not exist!"
    exit 1
  fi

  outf="firmware.bin"

  checkDriverHash "$1"

  offset="${firmw_offsets[$driver_hash]}"
  size="${firmw_sizes[$driver_hash]}"
  comp_method="${compression[$driver_hash]}"

  extractFirmware "$1" "$outf" "$offset" "$size" "$comp_method"

  checkFirmwareHash "$outf"

  echo ""
  exit 0
}

main "$@"
