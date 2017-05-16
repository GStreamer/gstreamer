plugin_LTLIBRARIES = libgstpcapparse.la

libgstpcapparse_la_SOURCES = \
	gstpcapparse.c gstirtspparse.c plugin.c

noinst_HEADERS = \
	gstpcapparse.h gstirtspparse.h

libgstpcapparse_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstpcapparse_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(WINSOCK2_LIBS)
libgstpcapparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
