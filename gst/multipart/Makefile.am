plugin_LTLIBRARIES = libgstmultipart.la

libgstmultipart_la_SOURCES = multipart.c multipartdemux.c multipartmux.c
libgstmultipart_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstmultipart_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstmultipart_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmultipart_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = multipartdemux.h multipartmux.h
