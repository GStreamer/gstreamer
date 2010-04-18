plugin_LTLIBRARIES = libgstvideofilter.la

noinst_HEADERS = gstvideoflip.h gstvideobalance.h gstgamma.h

EXTRA_DIST = gstvideotemplate.c make_filter
CLEANFILES = gstvideoexample.c

libgstvideofilter_la_SOURCES = plugin.c \
			gstvideoflip.c \
			gstvideobalance.c \
			gstgamma.c
libgstvideofilter_la_CFLAGS = $(GST_CFLAGS) $(GST_CONTROLLER_CFLAGS) \
			$(GST_BASE_CFLAGS) \
			$(GST_PLUGINS_BASE_CFLAGS)
libgstvideofilter_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
			-lgstvideo-@GST_MAJORMINOR@ \
			-lgstinterfaces-@GST_MAJORMINOR@ \
			$(GST_CONTROLLER_LIBS) \
			$(GST_BASE_LIBS) $(GST_LIBS)
libgstvideofilter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(LIBM)
libgstvideofilter_la_LIBTOOLFLAGS = --tag=disable-static

gstvideoexample.c: $(srcdir)/make_filter $(srcdir)/gstvideotemplate.c
	$(srcdir)/make_filter Videoexample $(srcdir)/gstvideotemplate.c
