pcfiles = \
	gst-validate-@GST_API_VERSION@.pc

pcfiles_uninstalled = \
	gst-validate-@GST_API_VERSION@-uninstalled.pc

all-local: $(pcfiles) $(pcfiles_uninstalled)

### how to generate pc files
%-@GST_API_VERSION@.pc: %.pc
	cp $< $@
%-@GST_API_VERSION@-uninstalled.pc: %-uninstalled.pc
### the uninstalled libdir is depend of the build system used so set it here
### rather than hardcoding it in the file directly.
	$(AM_V_GEN) sed \
		-e "s|[@]validatelibdir[@]|$(abs_top_builddir)/gst/validate/.libs|" \
		$< > $@.tmp && mv $@.tmp $@


pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcfiles)

EXTRA_DIST = \
	gst-validate.pc.in \
	gst-validate-uninstalled.pc.in
CLEANFILES = $(pcfiles) $(pcfiles_uninstalled)
