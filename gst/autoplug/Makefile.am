plugindir = $(libdir)/gst

plugin_LTLIBRARIES = \
	libgststaticautoplug.la \
	libgststaticautoplugrender.la \
	libgstautoplugcache.la \
	libgstautoplugger.la

libgststaticautoplug_la_SOURCES = gststaticautoplug.c 
libgststaticautoplug_la_CFLAGS = $(GST_CFLAGS)
libgststaticautoplug_la_LIBADD = $(GST_LIBS)
libgststaticautoplug_la_LDFLAGS = -version-info $(GST_LIBVERSION)

libgststaticautoplugrender_la_SOURCES = gststaticautoplugrender.c
libgststaticautoplugrender_la_CFLAGS = $(GST_CFLAGS)
libgststaticautoplugrender_la_LIBADD = $(GST_LIBS)
libgststaticautoplugrender_la_LDFLAGS = -version-info $(GST_LIBVERSION)

libgstautoplugcache_la_SOURCES = gstautoplugcache.c
libgstautoplugcache_la_CFLAGS = $(GST_CFLAGS)
libgstautoplugcache_la_LIBADD = $(GST_LIBS)
libgstautoplugcache_la_LDFLAGS = -version-info $(GST_LIBVERSION)

libgstautoplugger_la_SOURCES = gstautoplugger.c
libgstautoplugger_la_CFLAGS = $(GST_CFLAGS)
libgstautoplugger_la_LIBADD = $(GST_LIBS)
libgstautoplugger_la_LDFLAGS = -version-info $(GST_LIBVERSION)

noinst_HEADERS = gststaticautoplug.h gststaticautoplugrender.h

noinst_PROGRAMS = autoplugtest

autoplugtest_CFLAGS = $(GST_CFLAGS)
autoplugtest_LDADD = $(GST_LIBS)
