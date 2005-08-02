### all of the standard pc files we need to generate
pcfiles = 							\
	gstreamer-@GST_MAJORMINOR@.pc				\
	gstreamer-base-@GST_MAJORMINOR@.pc			\
	gstreamer-controller-@GST_MAJORMINOR@.pc			\
	gstreamer-dataprotocol-@GST_MAJORMINOR@.pc

pcfiles_uninstalled =						\
	gstreamer-@GST_MAJORMINOR@-uninstalled.pc		\
	gstreamer-base-@GST_MAJORMINOR@-uninstalled.pc		\
	gstreamer-controller-@GST_MAJORMINOR@-uninstalled.pc	\
	gstreamer-dataprotocol-@GST_MAJORMINOR@-uninstalled.pc

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
	gstreamer-base.pc.in					\
	gstreamer-base-uninstalled.pc.in			\
	gstreamer-controller.pc.in					\
	gstreamer-controller-uninstalled.pc.in			\
	gstreamer-dataprotocol.pc.in				\
	gstreamer-dataprotocol-uninstalled.pc.in

CLEANFILES = $(pcfiles) $(pcfiles_uninstalled)
