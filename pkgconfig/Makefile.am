### all of the standard pc files we need to generate
pcfiles = 							\
	gstreamer-@GST_MAJORMINOR@.pc				\
	gstreamer-control-@GST_MAJORMINOR@.pc

pcfiles_uninstalled =						\
	gstreamer-@GST_MAJORMINOR@-uninstalled.pc		\
	gstreamer-control-@GST_MAJORMINOR@-uninstalled.pc

all-local: $(pcfiles) $(pcfiles_uninstalled)

### how to generate pc files
%-@GST_MAJORMINOR@.pc: %.pc
	cp $< $@
%-@GST_MAJORMINOR@-uninstalled.pc: %-uninstalled.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcfiles)

EXTRA_DIST =							\
	gstreamer.pc.in						\
	gstreamer-uninstalled.pc.in				\
	gstreamer-control.pc.in					\
	gstreamer-control-uninstalled.pc.in

CLEANFILES = $(pcfiles) $(pcfiles_uninstalled)
