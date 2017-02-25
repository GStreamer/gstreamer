
plugin_LTLIBRARIES = libgstrawparse.la

libgstrawparse_la_SOURCES = \
	gstunalignedaudioparse.c \
	gstunalignedvideoparse.c \
	gstrawbaseparse.c \
	gstrawaudioparse.c \
	gstrawvideoparse.c \
	plugin.c
libgstrawparse_la_CFLAGS = \
		$(GST_PLUGINS_BASE_CFLAGS) \
		$(GST_BASE_CFLAGS) \
		$(GST_CFLAGS)
libgstrawparse_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstrawparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrawparse_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	unalignedaudio.h \
	unalignedvideo.h \
	gstunalignedaudioparse.h \
	gstunalignedvideoparse.h \
	gstrawbaseparse.h \
	gstrawaudioparse.h \
	gstrawvideoparse.h
