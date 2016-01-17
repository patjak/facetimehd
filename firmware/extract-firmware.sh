#!/bin/bash

# firmware header bytestring [bytes 04-32]
fw_bytes_header="feffffeafeffffeafeffffeafeffffeafeffffeafeffffeafeffffea"
fw_bytes_footer="00000000ffffffff"

# Known driver hashes
#
# NOTE: use sha256 checksums as they are more robust that md5 against collisions
hash_drv_wnd_105='6ec37d48c0764ed059dd49f472456a4f70150297d6397b7cc7965034cf78627e'
hash_drv_wnd_138='7044344593bfc08ab9b41ab691213bca568c8d924d0e05136b537f66b3c46f31'
hash_drv_osx_143_1='4667e6828f6bfc690a39cf9d561369a525f44394f48d0a98d750931b2f3f278b'
hash_drv_osx_143_2='d4650346c940dafdc50e5fcbeeeffe074ec359726773e79c0cfa601cec6b1f08'
hash_drv_osx_143_3='387097b5133e980196ac51504a60ae1ad8bab736eb0070a55774925ca0194892'

hash_fw_wnd_105='dabb8cf8e874451ebc85c51ef524bd83ddfa237c9ba2e191f8532b896594e50e'
hash_fw_wnd_138='ed75dc37b1a0e19949e9e046a629cb55deb6eec0f13ba8fd8dd49b5ccd5a800e'
hash_fw_osx_143_1='e3e6034a67dfdaa27672dd547698bbc5b33f47f1fc7f5572a2fb68ea09d32d3d'
hash_fw_osx_143_2='504fcf1565bf10d61b31a12511226ae51991fb55d480f82de202a2f7ee9c966e'

# Driver names
declare -A known_hashes=(
  ["$hash_drv_wnd_105"]='Windows Boot Camp 5.1.5722'
  ["$hash_drv_wnd_138"]='Windows Boot Camp Update Jul 29, 2015'
  ["$hash_drv_osx_143_1"]='OS X, El Capitan'
  ["$hash_drv_osx_143_2"]='OS X, El Capitan 10.11.2'
)

# Offset in bytes of the firmware inside the driver
declare -A firmw_offsets=(
  ["$hash_drv_wnd_105"]=78208
  ["$hash_drv_wnd_138"]=85296
  ["$hash_drv_osx_143_1"]=81920
  ["$hash_drv_osx_143_2"]=81920
)

# Size in bytes of the firmware inside the driver 
declare -A firmw_sizes=(
  ["$hash_drv_wnd_105"]=1523716
  ["$hash_drv_wnd_138"]=1421316
  ["$hash_drv_osx_143_1"]=603715
  ["$hash_drv_osx_143_2"]=603715
)

# Compression method used to store the firmware inside the driver
declare -A compression=(
  ["$hash_drv_wnd_105"]='cat'
  ["$hash_drv_wnd_138"]='cat'
  ["$hash_drv_osx_143_1"]='gzip'
  ["$hash_drv_osx_143_2"]='gzip'
)

declare -A firmw_hashes=(
  ["$hash_fw_wnd_105"]='1.05'
  ["$hash_fw_wnd_138"]='1.38'
  ["$hash_fw_osx_143_1"]='1.43.0-a'
  ["$hash_fw_osx_143_2"]='1.43.0-b'
)

printHelp()
{
  cat <<HELP_DOC
Usage: extract-firmware [OPTIONS]

OPTION:

  -h  --help          Print this help message and exit.

  --dmg DMG_FILE      Decompress the dmg file DMG_FILE, and extract the
                      OS X camrea driver.

  -i                  Ignore the firmware checksums and verify only the
                      binary header and footer of the extracted firmeare.

  -x DRV_FILE         Extract the firmware from the driver DRV_FILE

NOTES:

  At the moment only two drivers are currently available:

   - AppleCameraInterface: this is the OS X native driver, it can be found
     in a OS X installation under the following system directory
     /System/Library/Extensions/AppleCameraInterface.kext/Contents/MacOS/

   - AppleCamera.sys: this comes within the bootcamp windows driver package.
     You can download it from http://support.apple.com/downloads/DL1831/".

  Currently only the OS X firmware is working

HELP_DOC
}

msg()
{
  echo "==> $*"
}

msg2()
{
  echo " --> $*"
}

err()
{
  echo "Error: $*"
}

hasProgram()
{
  if ! which "$1" &> /dev/null; then
    err "'$1' needed but not found!"
    exit 1
  fi
}

checkDmgPrerequisites()
{
  hasProgram "7z"
  hasProgram "cpio"
  hasProgram "head"
  hasProgram "mkdir"
  hasProgram "pbzx"
  hasProgram "sha256sum"
  hasProgram "tail"
  hasProgram "xar"
}

checkPrerequisites()
{
  hasProgram "awk"
  hasProgram "dd"
  hasProgram "sha256sum"
  hasProgram "zcat"
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

  err "Mismatching driver hash for $1"
  err "The unknown hash is ${driver_hash}"
	err "No firmware extracted!"
  exit 1
}

checkFirmwareHash()
{
  # computing the hash for the input file
  fw_hash="$(getCheckSum $1)"

  # checking if it is among the known hashes
  for cur_hash in "${!firmw_hashes[@]}"; do
    if [[ "$fw_hash" == "$cur_hash" ]]; then
      msg2 "Extracted firmware version ${firmw_hashes[$cur_hash]}"
      return 0
    fi
  done

  err "Mismatching firmware hash ${firm_hash}"
  return 1
}

checkFirmwareHexdump()
{
  if ! which hexdump &> /dev/null; then
    err "You need to install 'hexdump' in order to check a firmware with unknown hash!"
    return 1
  fi

  header=$(hexdump -v -e '"" /1 "%02x"' "$1" -s 4 -n 28)
  footer=$(hexdump -v -e '"" /1 "%02x"' "$1" | tail -c 16)

  if [[ "${header}" != "${fw_bytes_header}" ]]; then
    err "The extracted firmware does not seem good (wrong header)"
    return 1
  elif [[ "${footer}" != "${fw_bytes_footer}" ]]; then
    err "The extracted firmware does not seem good (wrong footer)"
    return 1
  else
    msg2 "You're lucky, the firmware looks good, " \
         "but it could also not work... use it at your own risk!"
  fi
}

extractFirmware()
{
  msg "Extracting firmware..."
  dd bs=1 skip=$3 count=$4 if=$1 of="$2.tmp" &> /dev/null

  msg2 "Decompressing the firmware using $5..."
  case "$5" in
    "gzip")
      zcat "$2.tmp" > "$2"
      ;;
    "cat")
      cat "$2.tmp" > "$2"
      ;;
  esac

  msg2 "Deleting temporary files..."
  rm "$2.tmp"
}

decompress_dmg()
{
  msg "Extracting the driver from $1..."

  msg2 "Creating temporary directories..."
  mkdir -p "${_main_dir}/temp"
  cd "${_main_dir}/temp"

  msg2 "Decompressing the image..."
  7z e -y "${_main_dir}/$1" "5.hfs" > /dev/null

  msg2 "Extracting update package..."
  tail -c +189001729 "5.hfs" | head -c 1469917156 > OSXUpd.xar
  rm -f "5.hfs"

  msg2 "Uncompressing XAR archive..."
  xar -x -f "OSXUpd.xar"
  rm -f "OSXUpd.xar"

  msg2 "Decoding Payload..."
  pbzx "OSXUpd"*.pkg"/Payload" > /dev/null
  rm "OSXUpdCombo10.11.2.pkg/Payload"

  msg2 "Decompressing archives..."
  cd "OSXUpdCombo10.11.2.pkg" 
  find . -name "Payload.part*.xz" -exec xz --decompress --verbose {} \;
  cat "Payload.part"* | cpio -id &> /dev/null
  cp "./System/Library/Extensions/AppleCameraInterface.kext/Contents/MacOS/AppleCameraInterface" \
     "${_main_dir}"
  msg2 "Cleaning up..."
  rm -rf "${_main_dir}/temp"
}

extract_from_osx()
{
 
  echo "" 
  checkDriverHash "$1"

  offset="${firmw_offsets[$driver_hash]}"
  size="${firmw_sizes[$driver_hash]}"
  comp_method="${compression[$driver_hash]}"

  extractFirmware "$1" "firmware.bin" "$offset" "$size" "$comp_method"

  if ! checkFirmwareHash "firmware.bin"; then
    if [[ -z "$skip_sums" ]]; then
	    err "No firmware extracted!"
      exit 1
    else
      msg2 "Ignoring hashes and check the firmware header..."
      checkFirmwareHexdump "firmware.bin" then
    fi
  fi
}

main()
{
  echo ""

  # Parsing arguments
  while [[ $# > 0 ]]; do
    case $1 in
      -h|--help)
        printHelp
        exit 1
        ;;
      --dmg)
        dmg_file="$2"
        shift
        ;;
      -x)
        drv_file="$2"
        shift
        ;;
      -i|--ignore-hashes)
        skip_sums="1"
    esac
    shift
  done

  checkPrerequisites

  if [[ ! -z "$dmg_file" ]]; then
    checkDmgPrerequisites
    decompress_dmg "$dmg_file" 
  fi

  cd "${_main_dir}"

  if [[ ! -z "$drv_file" ]]; then
    extract_from_osx "$drv_file"
  fi

  echo ""
  exit 0
}

_main_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

main "$@"
