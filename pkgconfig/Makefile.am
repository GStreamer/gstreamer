pcfiles = \
	gstreamer-rtsp-server-@GST_API_VERSION@.pc
	
pcfiles_uninstalled = \
	gstreamer-rtsp-server-@GST_API_VERSION@-uninstalled.pc

all-local: $(pcfiles) $(pcfiles_uninstalled)

### how to generate pc files
%-@GST_API_VERSION@.pc: %.pc
	cp $< $@
%-@GST_API_VERSION@-uninstalled.pc: %-uninstalled.pc
### the uninstalled libdir is depend of the build system used so set it here
### rather than hardcoding it in the file directly.
	$(AM_V_GEN) sed \
		-e "s|[@]rtspserverlibdir[@]|$(abs_top_builddir)/gst/rtsp-server/.libs|" \
		$< > $@.tmp && mv $@.tmp $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcfiles)

EXTRA_DIST = \
	gstreamer-rtsp-server.pc.in \
	gstreamer-rtsp-server-uninstalled.pc.in
CLEANFILES = $(pcfiles) $(pcfiles_uninstalled)
