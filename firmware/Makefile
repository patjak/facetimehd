SHELL = /bin/bash

DMG=osxupd10.11.2.dmg
OSX_DRV:=AppleCameraInterface
RANGE=420107885-421933300
URL=https://support.apple.com/downloads/DL1849/en_US/$(DMG)

FILE=./System/Library/Extensions/AppleCameraInterface.kext/Contents/MacOS/$(OSX_DRV)
FW_DIR=/usr/lib/firmware/facetimehd

all: $(OSX_DRV)
	@./extract-firmware.sh -x "$(OSX_DRV)"

$(OSX_DRV):
	@# Ty to wvengen, see: https://github.com/patjak/bcwc_pcie/issues/14#issuecomment-167446787
	@echo "Dowloading the driver, please wait..."
	@(curl -s -L -r "$(RANGE)" "$(URL)" | xzcat -q | cpio --format odc -i -d "$(FILE)") &> /dev/null || true
	@mv "$(FILE)" .
	@rm -Rf "./System"

install:
	@echo "Copying firmware into '$(DESTDIR)/$(FW_DIR)'"
	@mkdir -p "$(DESTDIR)/$(FW_DIR)"
	@cp -f "firmware.bin" "$(DESTDIR)/$(FW_DIR)"

.PHONY: clean
clean:
	rm -f AppleCamera{Interface,.sys}
	rm -f firmware.bin

