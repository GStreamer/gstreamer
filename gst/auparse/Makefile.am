plugin_LTLIBRARIES = libgstauparse.la

libgstauparse_la_SOURCES = gstauparse.c
libgstauparse_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstauparse_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(GST_LIBS) $(GST_BASE_LIBS)
libgstauparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstauparse_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstauparse.h
