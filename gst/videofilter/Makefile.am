
plugin_LTLIBRARIES = libgstvideofilter.la libgstvideoflip.la \
	libgstvideobalance.la
noinst_LTLIBRARIES = libgstvideoexample.la

noinst_HEADERS = gstvideofilter.h gstvideoexample.h gstvideoflip.h gstvideobalance.h

EXTRA_DIST = gstvideotemplate.c make_filter
EXTRA_CLEAN = gstvideoexample.c

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
libgstvideobalance_la_CFLAGS = $(GST_CFLAGS)
libgstvideobalance_la_LIBADD =
libgstvideobalance_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lm


# no, this doesn't work if $(srcdir) != (builddir)
gstvideoexample.c: $(srcdir)/make_filter $(srcdir)/gstvideotemplate.c
	$(srcdir)/make_filter Videoexample

