lib_LTLIBRARIES = libgstmedia-info-@GST_MAJORMINOR@.la

libgstmedia_info_@GST_MAJORMINOR@_la_SOURCES = media-info.c media-info-priv.c

libgstmedia_info_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGIN_CFLAGS)
libgstmedia_info_@GST_MAJORMINOR@_la_LIBADD = \
	$(GST_LIBS) $(GST_PLUGIN_LIBS)
libgstmedia_info_@GST_MAJORMINOR@_la_LDFLAGS = \
	-version-info @GST_PLUGINS_LIBVERSION@


libincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/media-info
libinclude_HEADERS = media-info.h

noinst_PROGRAMS = media-info-test
noinst_HEADERS = media-info-priv.h

media_info_test_SOURCES = media-info-test.c
media_info_test_CFLAGS = $(GST_CFLAGS)
media_info_test_LDADD = $(GST_LIBS) libgstmedia-info-@GST_MAJORMINOR@.la
