### all of the standard pc files we need to generate
pcverfiles =  \
	gstreamer-plugins-base-@GST_MAJORMINOR@.pc
pcverfiles_uninstalled = \
	gstreamer-plugins-base-@GST_MAJORMINOR@-uninstalled.pc

all-local: $(pcverfiles) $(pcverfiles_uninstalled)

### how to generate versioned .pc files from .pc files in this dir
%-@GST_MAJORMINOR@.pc: %.pc
	cp $< $@
%-@GST_MAJORMINOR@-uninstalled.pc: %-uninstalled.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcverfiles)

CLEANFILES = $(pcverfiles) $(pcverfiles_uninstalled)
pcinfiles = \
           gstreamer-plugins-base.pc.in gstreamer-plugins-base-uninstalled.pc.in

DISTCLEANFILES = $(pcinfiles:.in=)
EXTRA_DIST = $(pcinfiles)
