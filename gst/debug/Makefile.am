
plugin_LTLIBRARIES = libgstefence.la libgstnavigationtest.la libgstdebug.la

noinst_HEADERS = efence.h gstnavigationtest.h gstnavseek.h

libgstefence_la_SOURCES = efence.c
libgstefence_la_CFLAGS = $(GST_CFLAGS)
libgstefence_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstnavigationtest_la_SOURCES = gstnavigationtest.c
libgstnavigationtest_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/gst/videofilter
libgstnavigationtest_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstdebug_la_SOURCES = \
	gstdebug.c \
	breakmydata.c \
	negotiation.c \
	gstnavseek.c \
	progressreport.c

libgstdebug_la_CFLAGS = $(GST_CFLAGS)
libgstdebug_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
