plugin_LTLIBRARIES = libgstaudioconvert.la

libgstaudioconvert_la_SOURCES = \
	gstaudioconvert.c \
	audioconvert.c \
	gstchannelmix.c \
	bufferframesconvert.c \
	plugin.c

libgstaudioconvert_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstaudioconvert_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudioconvert_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la \
	$(GST_LIBS) $(GST_BASE_LIBS)

noinst_HEADERS = \
	gstaudioconvert.h \
	audioconvert.h \
	gstchannelmix.h \
	plugin.h

#TESTS = channelmixtest
#noinst_PROGRAMS = channelmixtest

#channelmixtest_CFLAGS = $(GST_CFLAGS)
#channelmixtest_LDADD = libgstaudioconvert.la \
#	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la
#channelmixtest_LDFLAGS = $(GST_LIBS)
