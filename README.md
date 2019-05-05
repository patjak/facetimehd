facetimehd
==========

Linux driver for the Facetime HD (Broadcom 1570) PCIe webcam
found in recent Macbooks.

This driver is experimental. Use at your own risk.

See the [Wiki][wiki] for more information:

[wiki]: https://github.com/patjak/bcwc_pcie/wiki

# How to enable the Chromium/Skype Workaround?
This driver contains workaround code for cheaply restoring skype and chromium
compatibility by artificially restricting the capabilities of the facetimehd webcam.

To enable this workaround pass the module parameter `enable_chromium_workaround`
when loading the module.

    sudo modprobe facetimehd enable_chromium_workaround=1

To permanently enable the workaround one can add a file
`/etc/modprobe.d/facetimehd` with the following content:

    option facetimehd enable_chromium_workaround=1

Then, if the webcam is required at its full capabilities via the proper
v4l2 API one can simply disable the workaround again temporarily by reloading
the driver with the appropriate flag:

    sudo rmmod facetimehd
    sudo modprobe facetimehd enable_chromium_workaround=0

Technically skype and chromium support was broken due to the fact that these
programs do not support the v4l2 stepwise resolution API provided by the
facetimehd driver. The clean fix will be for chrome/skype to support the new
API in the long term. For now for convenience of users this workaround
restores compatibility in a very cheap way. Technical details are described
in https://github.com/patjak/bcwc_pcie/issues/123
