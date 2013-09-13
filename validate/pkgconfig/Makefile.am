pcfiles = \
	gst-validate-@GST_API_VERSION@.pc

pcfiles_uninstalled = \
	gst-validate-@GST_API_VERSION@-uninstalled.pc

all-local: $(pcfiles) $(pcfiles_uninstalled)

### how to generate pc files
%-@GST_API_VERSION@.pc: %.pc
	cp $< $@
%-@GST_API_VERSION@-uninstalled.pc: %-uninstalled.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcfiles)

EXTRA_DIST = \
	gst-validate.pc.in \
	gst-validate-uninstalled.pc.in
CLEANFILES = $(pcfiles) $(pcfiles_uninstalled)
