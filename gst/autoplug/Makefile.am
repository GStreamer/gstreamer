filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgststaticautoplug.la libgststaticautoplugrender.la \
	libgstautoplugcache.la libgstautoplugger.la

libgststaticautoplug_la_SOURCES = gststaticautoplug.c 
libgststaticautoplugrender_la_SOURCES =	gststaticautoplugrender.c
libgstautoplugcache_la_SOURCES = gstautoplugcache.c
libgstautoplugger_la_SOURCES = gstautoplugger.c

libgststaticautoplug_la_LDFLAGS = -version-info $(GST_LIBVERSION)
libgststaticautoplugrender_la_LDFLAGS = -version-info $(GST_LIBVERSION)
libgstautoplugcache_la_LDFLAGS = -version-info $(GST_LIBVERSION)
libgstautoplugger_la_LDFLAGS = -version-info $(GST_LIBVERSION)

libgststaticautoplug_la_CFLAGS = $(GLIB_CFLAGS) $(XML_CFLAGS)
libgststaticautoplugrender_la_CFLAGS = $(GLIB_CFLAGS) $(XML_CFLAGS)
libgstautoplugcache_la_CFLAGS = $(GLIB_CFLAGS) $(XML_CFLAGS)
libgstautoplugger_la_CFLAGS = $(GLIB_CFLAGS) $(XML_CFLAGS)

noinst_HEADERS = gststaticautoplug.h gststaticautoplugrender.h

noinst_PROGRAMS = autoplugtest

autoplugtest_CFLAGS = $(GST_CFLAGS) $(GLIB_CFLAGS) $(XML_CFLAGS) $(GST_CFLAGS)
autoplugtest_LDADD = $(GST_LIBS) $(GLIB_LIBS) $(XML_LIBS)
