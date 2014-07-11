
### all of the standard pc files we need to generate
pcverfiles =  \
	gstreamer-plugins-bad-@GST_API_VERSION@.pc \
	gstreamer-codecparsers-@GST_API_VERSION@.pc \
	gstreamer-insertbin-@GST_API_VERSION@.pc \
	gstreamer-mpegts-@GST_API_VERSION@.pc

pcverfiles_uninstalled = \
	gstreamer-plugins-bad-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-codecparsers-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-insertbin-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-mpegts-@GST_API_VERSION@-uninstalled.pc

if HAVE_GST_GL
pcverfiles += gstreamer-gl-@GST_API_VERSION@.pc
pcverfiles_uninstalled += gstreamer-gl-@GST_API_VERSION@-uninstalled.pc
endif

all-local: $(pcverfiles) $(pcverfiles_uninstalled)

cp_verbose = $(cp_verbose_$(V))
cp_verbose_ = $(cp_verbose_$(AM_DEFAULT_VERBOSITY))
cp_verbose_0 = @echo "  CP     $@";

### how to generate versioned .pc files from .pc files in this dir
%-@GST_API_VERSION@.pc: %.pc
	$(cp_verbose_0)cp $< $@
%-@GST_API_VERSION@-uninstalled.pc: %-uninstalled.pc
	$(cp_verbose_0)cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcverfiles)

CLEANFILES = $(pcverfiles) $(pcverfiles_uninstalled)
pcinfiles = \
           gstreamer-plugins-bad.pc.in gstreamer-plugins-bad-uninstalled.pc.in \
           gstreamer-codecparsers.pc.in gstreamer-codecparsers-uninstalled.pc.in \
           gstreamer-gl.pc.in gstreamer-gl-uninstalled.pc.in \
           gstreamer-insertbin.pc.in gstreamer-insertbin-uninstalled.pc.in \
           gstreamer-mpegts.pc.in gstreamer-mpegts-uninstalled.pc.in

DISTCLEANFILES = $(pcinfiles:.in=)
EXTRA_DIST = $(pcinfiles)
