plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstplaybin.la libgstdecodebin.la

libgstplaybin_la_SOURCES = gstplaybin.c gstplaybasebin.c gststreaminfo.c
libgstplaybin_la_CFLAGS = $(GST_CFLAGS)
libgstplaybin_la_LIBADD =
libgstplaybin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstdecodebin_la_SOURCES = gstdecodebin.c
libgstdecodebin_la_CFLAGS = $(GST_CFLAGS)
libgstdecodebin_la_LIBADD =
libgstdecodebin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_PROGRAMS = test decodetest test2 test3

test_LDADD = $(GST_LIBS) 
test_CFLAGS = $(GST_CFLAGS)

test2_LDADD = $(GST_LIBS) 
test2_CFLAGS = $(GST_CFLAGS)

test3_LDADD = $(GST_LIBS) 
test3_CFLAGS = $(GST_CFLAGS)

decodetest_LDADD = $(GST_LIBS) 
decodetest_CFLAGS = $(GST_CFLAGS)
