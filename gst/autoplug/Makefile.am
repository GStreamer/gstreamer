filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgststaticautoplug.la libgststaticautoplugrender.la \
	libgstautoplugcache.la

libgststaticautoplug_la_SOURCES =	\
	gststaticautoplug.c 
	
libgststaticautoplugrender_la_SOURCES =	\
	gststaticautoplugrender.c

libgstautoplugcache_la_SOURCES = gstautoplugcache.c

libgststaticautoplug_la_LDFLAGS = -version-info $(GST_LIBVERSION)
libgststaticautoplugrender_la_LDFLAGS = -version-info $(GST_LIBVERSION)
libgstautoplugcache_la_LDFLAGS = -version-info $(GST_LIBVERSION)

noinst_PROGRAMS = autoplugtest
autoplugtest_LDADD = $(GST_LIBS) 

