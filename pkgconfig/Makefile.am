### all of the standard pc files we need to generate
pcverfiles =  \
	gstreamer-plugins-good-@GST_API_VERSION@.pc
pcverfiles_uninstalled = \
	gstreamer-plugins-good-@GST_API_VERSION@-uninstalled.pc

### all-local: $(pcverfiles) $(pcverfiles_uninstalled)
all-local: $(pcverfiles_uninstalled)

cp_verbose = $(cp_verbose_$(V))
cp_verbose_ = $(cp_verbose_$(AM_DEFAULT_VERBOSITY))
cp_verbose_0 = @echo "  CP     $@";

### how to generate versioned .pc files from .pc files in this dir
%-@GST_API_VERSION@.pc: %.pc
	$(cp_verbose)cp $< $@
%-@GST_API_VERSION@-uninstalled.pc: %-uninstalled.pc
	$(cp_verbose)cp $< $@

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
