
plugin_LTLIBRARIES = libgstvideofilter.la libgstvideoflip.la \
	libgstvideobalance.la libgstgamma.la
noinst_LTLIBRARIES = libgstvideoexample.la

noinst_HEADERS = gstvideofilter.h gstvideoflip.h gstvideobalance.h

EXTRA_DIST = gstvideotemplate.c make_filter
CLEANFILES = gstvideoexample.c

libgstvideofilter_la_SOURCES = gstvideofilter.c
libgstvideofilter_la_CFLAGS = $(GST_CFLAGS)
libgstvideofilter_la_LIBADD =
libgstvideofilter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstvideoexample_la_SOURCES = gstvideoexample.c
libgstvideoexample_la_CFLAGS = $(GST_CFLAGS)
libgstvideoexample_la_LIBADD =
libgstvideoexample_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstvideoflip_la_SOURCES = gstvideoflip.c
libgstvideoflip_la_CFLAGS = $(GST_CFLAGS)
libgstvideoflip_la_LIBADD =
libgstvideoflip_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstvideobalance_la_SOURCES = gstvideobalance.c
libgstvideobalance_la_CFLAGS = $(GST_CFLAGS) $(LIBOIL_CFLAGS)
libgstvideobalance_la_LIBADD = $(top_builddir)/gst-libs/gst/libgstinterfaces-$(GST_MAJORMINOR).la
libgstvideobalance_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(LIBOIL_LIBS) -lm

libgstgamma_la_SOURCES = gstgamma.c
libgstgamma_la_CFLAGS = $(GST_CFLAGS)
libgstgamma_la_LIBADD =
libgstgamma_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lm

gstvideoexample.c: $(srcdir)/make_filter $(srcdir)/gstvideotemplate.c
	$(srcdir)/make_filter Videoexample $(srcdir)/gstvideotemplate.c
