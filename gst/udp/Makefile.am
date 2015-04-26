plugin_LTLIBRARIES = libgstudp.la

libgstudp_la_SOURCES = gstudp.c gstudpsrc.c gstudpsink.c gstmultiudpsink.c gstdynudpsink.c gstudpnetutils.c

libgstudp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_NET_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS)
libgstudp_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_NET_LIBS) $(GIO_LIBS)
libgstudp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstudp_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstudpsink.h gstudpsrc.h gstmultiudpsink.h gstdynudpsink.h gstudpnetutils.h

EXTRA_DIST = README

CLEANFILES = $(BUILT_SOURCES)
