plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = \
	libgstspider.la

libgstspider_la_SOURCES = \
	gstspider.c gstspideridentity.c \
	gstsearchfuncs.c
libgstspider_la_CFLAGS = $(GST_CFLAGS)
libgstspider_la_LIBADD =
libgstspider_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstspider.h gstsearchfuncs.h 

noinst_PROGRAMS = spidertest

spidertest_SOURCES = spidertest.c
spidertest_CFLAGS = $(GST_CFLAGS)
spidertest_LDADD = $(GST_LIBS)

