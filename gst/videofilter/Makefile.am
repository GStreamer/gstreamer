# noinst_LTLIBRARIES = libgstvideoexample.la
plugin_LTLIBRARIES = libgstvideoflip.la libgstvideobalance.la

noinst_HEADERS = gstvideoflip.h gstvideobalance.h

EXTRA_DIST = gstvideotemplate.c make_filter
CLEANFILES = gstvideoexample.c

# libgstvideoexample_la_SOURCES = gstvideoexample.c
# libgstvideoexample_la_CFLAGS = $(GST_CFLAGS)
# libgstvideoexample_la_LIBADD = libgstvideofilter-@GST_MAJORMINOR@.la $(GST_LIBS)
# libgstvideoexample_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstvideoflip_la_SOURCES = gstvideoflip.c
libgstvideoflip_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
			    $(GST_PLUGINS_BASE_CFLAGS)
libgstvideoflip_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ $(GST_BASE_LIBS) $(GST_LIBS)
libgstvideoflip_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstvideobalance_la_SOURCES = gstvideobalance.c
libgstvideobalance_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
			    $(GST_PLUGINS_BASE_CFLAGS)
libgstvideobalance_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ \
			-lgstinterfaces-@GST_MAJORMINOR@ $(GST_BASE_LIBS) $(GST_LIBS)
libgstvideobalance_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# libgstgamma_la_SOURCES = gstgamma.c
# libgstgamma_la_CFLAGS = $(GST_CFLAGS)
# libgstgamma_la_LIBADD =
# libgstgamma_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lm

gstvideoexample.c: $(srcdir)/make_filter $(srcdir)/gstvideotemplate.c
	$(srcdir)/make_filter Videoexample $(srcdir)/gstvideotemplate.c
