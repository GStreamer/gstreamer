plugin_LTLIBRARIES = libgstmusepack.la

libgstmusepack_la_SOURCES = \
	gstmusepackdec.c \
	gstmusepackreader.c
libgstmusepack_la_CFLAGS = $(MUSEPACK_CFLAGS) $(GST_CFLAGS)
libgstmusepack_la_LIBADD = $(MUSEPACK_LIBS) $(GST_LIBS)
libgstmusepack_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmusepack_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstmusepackdec.h \
	gstmusepackreader.h
