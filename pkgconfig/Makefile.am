### all of the standard pc files we need to generate
pcverfiles =  \
	gstreamer-plugins-bad-@GST_MAJORMINOR@.pc \
	gstreamer-codecparsers-@GST_MAJORMINOR@.pc

pcverfiles_uninstalled = \
	gstreamer-plugins-bad-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-codecparsers-@GST_MAJORMINOR@-uninstalled.pc

all-local: $(pcverfiles) $(pcverfiles_uninstalled)

cp_verbose = $(cp_verbose_$(V))
cp_verbose_ = $(cp_verbose_$(AM_DEFAULT_VERBOSITY))
cp_verbose_0 = @echo "  CP     $@";

### how to generate versioned .pc files from .pc files in this dir
%-@GST_MAJORMINOR@.pc: %.pc
	$(cp_verbose_0)cp $< $@
%-@GST_MAJORMINOR@-uninstalled.pc: %-uninstalled.pc
	$(cp_verbose_0)cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcverfiles)

CLEANFILES = $(pcverfiles) $(pcverfiles_uninstalled)
pcinfiles = \
           gstreamer-plugins-bad.pc.in gstreamer-plugins-bad-uninstalled.pc.in \
           gstreamer-codecparsers.pc.in gstreamer-codecparsers-uninstalled.pc.in

DISTCLEANFILES = $(pcinfiles:.in=)
EXTRA_DIST = $(pcinfiles)
