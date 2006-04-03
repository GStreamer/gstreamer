### all of the standard pc files we need to generate
pcverfiles =  \
	gstreamer-plugins-good-@GST_MAJORMINOR@.pc
pcverfiles_uninstalled = \
	gstreamer-plugins-good-@GST_MAJORMINOR@-uninstalled.pc

### all-local: $(pcverfiles) $(pcverfiles_uninstalled)
all-local: $(pcverfiles_uninstalled)

### how to generate versioned .pc files from .pc files in this dir
%-@GST_MAJORMINOR@.pc: %.pc
	cp $< $@
%-@GST_MAJORMINOR@-uninstalled.pc: %-uninstalled.pc
	cp $< $@

# do not install for now
# pkgconfigdir = $(libdir)/pkgconfig
# pkgconfig_DATA = $(pcverfiles)

CLEANFILES = $(pcverfiles) $(pcverfiles_uninstalled)
# pcinfiles = \
#           gstreamer-plugins-good.pc.in gstreamer-plugins-good-uninstalled.pc.in
pcinfiles = \
           gstreamer-plugins-good-uninstalled.pc.in

DISTCLEANFILES = $(pcinfiles:.in=)
EXTRA_DIST = $(pcinfiles)
