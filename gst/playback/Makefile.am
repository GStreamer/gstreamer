plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstplaybin.la libgstdecodebin.la

libgstplaybin_la_SOURCES = gstplaybin.c
libgstplaybin_la_CFLAGS = $(GST_CFLAGS)
libgstplaybin_la_LIBADD =
libgstplaybin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstdecodebin_la_SOURCES = gstdecodebin.c
libgstdecodebin_la_CFLAGS = $(GST_CFLAGS)
libgstdecodebin_la_LIBADD =
libgstdecodebin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_PROGRAMS = test decodetest

test_LDADD = $(GST_LIBS) 
test_CFLAGS = $(GST_CFLAGS)

decodetest_LDADD = $(GST_LIBS) 
decodetest_CFLAGS = $(GST_CFLAGS)
