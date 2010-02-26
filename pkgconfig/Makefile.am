### all of the standard pc files we need to generate
pcfiles = \
	gst-python-@GST_MAJORMINOR@.pc

pcfiles_uninstalled = \
	gst-python-@GST_MAJORMINOR@-uninstalled.pc

all-local: $(pcfiles) $(pcfiles_uninstalled)

cp_verbose = $(cp_verbose_$(V))
cp_verbose_ = $(cp_verbose_$(AM_DEFAULT_VERBOSITY))
cp_verbose_0 = @echo "  CP     $@";

### how to generate pc files
$(pcfiles): %-@GST_MAJORMINOR@.pc: %.pc
	$(cp_verbose_0)cp $< $@
$(pcfiles_uninstalled): %-@GST_MAJORMINOR@-uninstalled.pc: %-uninstalled.pc
	$(cp_verbose_0)cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcfiles)

EXTRA_DIST = \
	gst-python.pc.in \
	gst-python-uninstalled.pc.in

CLEANFILES = $(pcfiles) $(pcfiles_uninstalled)
