SHELL = /bin/bash

DMG:=osxupd10.11.2.dmg
OSX_DRV:=AppleCameraInterface
OSX_DRV_DIR:=System/Library/Extensions/AppleCameraInterface.kext/Contents/MacOS

RANGE:=420107885-421933300

URL:=https://support.apple.com/downloads/DL1849/en_US/$(DMG)
FILE:=$(OSX_DRV_DIR)/$(OSX_DRV)

ifneq ("$(wildcard /usr/lib/firmware)", "")
    FW_DIR_BASE:="/usr/lib/firmware"
else
    FW_DIR_BASE:="/lib/firmware"
endif

FW_DIR:="$(FW_DIR_BASE)/facetimehd"

all: $(OSX_DRV)
	@./extract-firmware.sh -x "$(OSX_DRV)"

$(OSX_DRV):
	@echo ""
	@echo "Checking dependencies for driver download..."
	@which curl xzcat cpio
	@echo ""
	@# Ty to wvengen, see: https://github.com/patjak/bcwc_pcie/issues/14#issuecomment-167446787
	@echo "Downloading the driver, please wait..."
	@(curl -s -L -r "$(RANGE)" "$(URL)" | xzcat -q | cpio --format odc -i -d "./$(FILE)") &> /dev/null || true
	@mv "$(FILE)" .
	@rmdir -p "$(OSX_DRV_DIR)"

install:
	@echo "Copying firmware into '$(DESTDIR)/$(FW_DIR)'"
	@install -dm755 "$(DESTDIR)/$(FW_DIR)"
	@install -m644 "firmware.bin" "$(DESTDIR)/$(FW_DIR)/firmware.bin"

.PHONY: clean
clean:
	rm -f AppleCamera{Interface,.sys}
	rm -f firmware.bin

