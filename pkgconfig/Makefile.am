### all of the standard pc files we need to generate
pcfiles = \
	gst-python-@GST_MAJORMINOR@.pc

pcfiles_uninstalled = \
	gst-python-@GST_MAJORMINOR@-uninstalled.pc

all-local: $(pcfiles) $(pcfiles_uninstalled)

### how to generate pc files
$(pcfiles): %-@GST_MAJORMINOR@.pc: %.pc
	cp $< $@
$(pcfiles_uninstalled): %-@GST_MAJORMINOR@-uninstalled.pc: %-uninstalled.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcfiles)

EXTRA_DIST = \
	gst-python.pc.in \
	gst-python-uninstalled.pc.in

CLEANFILES = $(pcfiles) $(pcfiles_uninstalled)
