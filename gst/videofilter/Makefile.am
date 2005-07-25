lib_LTLIBRARIES = libgstvideofilter-@GST_MAJORMINOR@.la
noinst_LTLIBRARIES = libgstvideoexample.la
plugin_LTLIBRARIES = libgstvideoflip.la

noinst_HEADERS = gstvideofilter.h gstvideoflip.h

EXTRA_DIST = gstvideotemplate.c make_filter
CLEANFILES = gstvideoexample.c

libgstvideofilter_@GST_MAJORMINOR@_la_SOURCES = gstvideofilter.c gstvideofilter.h
libgstvideofilter_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS)
libgstvideofilter_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIBS)

libgstvideoexample_la_SOURCES = gstvideoexample.c
libgstvideoexample_la_CFLAGS = $(GST_CFLAGS)
libgstvideoexample_la_LIBADD = libgstvideofilter-@GST_MAJORMINOR@.la
libgstvideoexample_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstvideoflip_la_SOURCES = gstvideoflip.c
libgstvideoflip_la_CFLAGS = $(GST_CFLAGS)
libgstvideoflip_la_LIBADD = libgstvideofilter-@GST_MAJORMINOR@.la
libgstvideoflip_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# libgstvideobalance_la_SOURCES = gstvideobalance.c
# libgstvideobalance_la_CFLAGS = $(GST_CFLAGS) $(LIBOIL_CFLAGS)
# libgstvideobalance_la_LIBADD = $(top_builddir)/gst-libs/gst/libgstinterfaces-$(GST_MAJORMINOR).la
# libgstvideobalance_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(LIBOIL_LIBS) -lm

# libgstgamma_la_SOURCES = gstgamma.c
# libgstgamma_la_CFLAGS = $(GST_CFLAGS)
# libgstgamma_la_LIBADD =
# libgstgamma_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lm

gstvideoexample.c: $(srcdir)/make_filter $(srcdir)/gstvideotemplate.c
	$(srcdir)/make_filter Videoexample $(srcdir)/gstvideotemplate.c
