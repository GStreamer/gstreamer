
plugin_LTLIBRARIES = libgstrawparse.la

libgstrawparse_la_SOURCES = \
	gstunalignedaudioparse.c \
	gstunalignedvideoparse.c \
	gstaudioparse.c \
	gstvideoparse.c \
	gstrawbaseparse.c \
	gstrawaudioparse.c \
	gstrawvideoparse.c \
	plugin.c
libgstrawparse_la_CFLAGS = \
		$(GST_PLUGINS_BASE_CFLAGS) \
		$(GST_BASE_CFLAGS) \
		$(GST_CFLAGS)
libgstrawparse_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-@GST_API_VERSION@ -lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstrawparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrawparse_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	unalignedaudio.h \
	unalignedvideo.h \
	gstunalignedaudioparse.h \
	gstunalignedvideoparse.h \
	gstaudioparse.h \
	gstvideoparse.h \
	gstrawbaseparse.h \
	gstrawaudioparse.h \
	gstrawvideoparse.h
