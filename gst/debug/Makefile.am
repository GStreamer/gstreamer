plugin_LTLIBRARIES = libgstefence.la libgstdebug.la libgstnavigationtest.la 

noinst_HEADERS = efence.h gstnavigationtest.h gstnavseek.h tests.h

libgstefence_la_SOURCES = efence.c
libgstefence_la_CFLAGS = $(GST_CFLAGS)
libgstefence_la_LIBADD = $(GST_LIBS)
libgstefence_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstnavigationtest_la_SOURCES = gstnavigationtest.c
libgstnavigationtest_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/gst/videofilter
libgstnavigationtest_la_LIBADD = $(GST_LIBS) $(top_builddir)/gst/videofilter/libgstvideofilter-@GST_MAJORMINOR@.la
libgstnavigationtest_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
 
libgstdebug_la_SOURCES = \
	gstdebug.c \
 	breakmydata.c \
 	progressreport.c \
	gstnavseek.c \
 	tests.c \
 	testplugin.c
# 	negotiation.c

libgstdebug_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstdebug_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstdebug_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
