librarydir = $(libdir)

library_LTLIBRARIES = libgstplay-@GST_MAJORMINOR@.la

libgstplay_@GST_MAJORMINOR@_la_SOURCES = gstplay.c

libgstplay_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/play
libgstplay_@GST_MAJORMINOR@include_HEADERS = gstplay.h

libgstplay_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_CFLAGS)
libgstplay_@GST_MAJORMINOR@_la_LIBADD =	
	$(GST_LIBS) $(GST_PLUGINS_LIBS) \
	$(GST_CONTROL_LIBS) \
	 $(top_builddir)/gst-libs/gst/libgstinterfaces-$(GST_MAJORMINOR).la
libgstplay_@GST_MAJORMINOR@_la_LDFLAGS = \
		-version-info @GST_PLUGINS_LIBVERSION@

