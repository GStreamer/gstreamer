plugindir = $(libdir)/gst

plugin_LTLIBRARIES = \
	libgststaticautoplug.la \
	libgststaticautoplugrender.la \
	libgstautoplugcache.la \
	libgstautoplugger.la \
	libgstspider.la

libgststaticautoplug_la_SOURCES = gststaticautoplug.c 
libgststaticautoplug_la_CFLAGS = $(GST_CFLAGS)
libgststaticautoplug_la_LIBADD =
libgststaticautoplug_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgststaticautoplugrender_la_SOURCES = gststaticautoplugrender.c
libgststaticautoplugrender_la_CFLAGS = $(GST_CFLAGS)
libgststaticautoplugrender_la_LIBADD =
libgststaticautoplugrender_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstautoplugcache_la_SOURCES = gstautoplugcache.c
libgstautoplugcache_la_CFLAGS = $(GST_CFLAGS)
libgstautoplugcache_la_LIBADD =
libgstautoplugcache_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstautoplugger_la_SOURCES = gstautoplugger.c
libgstautoplugger_la_CFLAGS = $(GST_CFLAGS)
libgstautoplugger_la_LIBADD =
libgstautoplugger_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstspider_la_SOURCES = gstspider.c gstspideridentity.c gstsearchfuncs.c
libgstspider_la_CFLAGS = $(GST_CFLAGS)
libgstspider_la_LIBADD =
libgstspider_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gststaticautoplug.h gststaticautoplugrender.h \
	gstspider.h gstspideridentity.h gstsearchfuncs.h

noinst_PROGRAMS = autoplugtest spidertest

autoplugtest_CFLAGS = $(GST_CFLAGS)
autoplugtest_LDADD = $(GST_LIBS)

spidertest_SOURCES = spidertest.c
spidertest_CFLAGS = $(GST_CFLAGS)
spidertest_LDADD = $(GST_LIBS)

