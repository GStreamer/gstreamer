pcfiles = \
	gst-editing-services-@GST_API_VERSION@.pc

pcfiles_uninstalled = \
	gst-editing-services-@GST_API_VERSION@-uninstalled.pc

all-local: $(pcfiles) $(pcfiles_uninstalled)

### how to generate pc files
%-@GST_API_VERSION@.pc: %.pc
	cp $< $@
%-@GST_API_VERSION@-uninstalled.pc: %-uninstalled.pc
### the uninstalled libdir is depend of the build system used so set it here
### rather than hardcoding it in the file directly.
	$(AM_V_GEN) sed \
		-e "s|[@]geslibdir[@]|$(abs_top_builddir)/ges/.libs|" \
		$< > $@.tmp && mv $@.tmp $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcfiles)

EXTRA_DIST = \
	gst-editing-services.pc.in \
	gst-editing-services-uninstalled.pc.in
CLEANFILES = $(pcfiles) $(pcfiles_uninstalled)
