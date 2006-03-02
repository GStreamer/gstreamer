plugin_LTLIBRARIES = libgstapetag.la

libgstapetag_la_SOURCES = gstapedemux.c gsttagdemux.c
libgstapetag_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstapetag_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstapetag_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstapedemux.h gsttagdemux.h
