pcfiles = \
	gst-rtsp-server-@GST_MAJORMINOR@.pc
	
all-local: $(pcfiles)

### how to generate pc files
%-@GST_MAJORMINOR@.pc: %.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcfiles)

EXTRA_DIST = gst-rtsp-server.pc.in
CLEANFILES = $(pcfiles)
