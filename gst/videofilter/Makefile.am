plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstvideofilter.la
noinst_LTLIBRARIES = libgstvideoexample.la

noinst_HEADERS = gstvideofilter.h gstvideoexample.h

EXTRA_DIST = gstvideotemplate.c gstvideotemplate.h make_filter

libgstvideofilter_la_SOURCES = gstvideofilter.c
libgstvideofilter_la_CFLAGS = $(GST_CFLAGS)
libgstvideofilter_la_LIBADD =
libgstvideofilter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstvideoexample_la_SOURCES = gstvideoexample.c
libgstvideoexample_la_CFLAGS = $(GST_CFLAGS)
libgstvideoexample_la_LIBADD =
libgstvideoexample_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)


# no, this doesn't work if $(srcdir) != (builddir)
gstvideoexample.c gstvideoexample.h: $(srcdir)/make_filter \
		$(srcdir)/gstvideotemplate.c $(srcdir)/gstvideotemplate.h
	$(srcdir)/make_filter Videoexample

