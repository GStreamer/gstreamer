plugin_LTLIBRARIES = libgstmng.la

libgstmng_la_SOURCES = gstmng.c gstmngdec.c gstmngenc.c
libgstmng_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstmng_la_LIBADD = $(GST_LIBS) $(LIBMNG_LIBS)
libgstmng_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmng_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstmngdec.h gstmngenc.h
