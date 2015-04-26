plugin_LTLIBRARIES = libgstsubenc.la

libgstsubenc_la_SOURCES = \
	gstsrtenc.c \
	gstsubenc.c \
	gstwebvttenc.c
libgstsubenc_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstsubenc_la_LIBADD = $(GST_BASE_LIBS)
libgstsubenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsubenc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstsrtenc.h \
	gstwebvttenc.h
