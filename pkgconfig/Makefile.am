### all of the standard pc files we need to generate
pcfiles = \
	gstreamer-@GST_MAJORMINOR@.pc \
	gstreamer-control-@GST_MAJORMINOR@.pc

pcfiles_uninstalled = \
	gstreamer-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-control-@GST_MAJORMINOR@-uninstalled.pc

all-local: $(pcfiles) $(pcfiles_uninstalled)

### how to generate pc files
$(pcfiles): %-@GST_MAJORMINOR@.pc: %.pc
	cp $< $@
$(pcfiles_uninstalled): %-@GST_MAJORMINOR@-uninstalled.pc: %-uninstalled.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcfiles)

EXTRA_DIST = $(pcfiles:.pc=.pc.in) \
	     $(pcfiles_uninstalled:.pc=.pc.in)
