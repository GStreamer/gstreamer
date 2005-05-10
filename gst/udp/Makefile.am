
plugin_LTLIBRARIES = libgstudp.la

libgstudp_la_SOURCES = gstudp.c gstudpsrc.c gstudpsink.c
libgstudp_la_CFLAGS = $(GST_CFLAGS)
libgstudp_la_LIBADD =
libgstudp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_BASE_LIBS)

noinst_HEADERS = gstudpsink.h gstudpsrc.h gstudp.h

EXTRA_DIST = README
