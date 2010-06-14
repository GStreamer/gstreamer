plugin_LTLIBRARIES = libgstdtsdec.la

libgstdtsdec_la_SOURCES = gstdtsdec.c
libgstdtsdec_la_CFLAGS = $(GST_CFLAGS) $(ORC_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstdtsdec_la_LIBADD = $(DTS_LIBS) $(ORC_LIBS) $(GST_PLUGINS_BASE_LIBS)  \
	-lgstaudio-@GST_MAJORMINOR@
libgstdtsdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdtsdec_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstdtsdec.h
