libgstinterfacesincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/mixer

libgstinterfacesinclude_HEADERS = \
	mixer.h \
	mixertrack.h

lib_LTLIBRARIES = libgstmixer.la

libgstmixer_la_SOURCES = \
	mixer.c \
	mixertrack.c
libgstmixer_la_LIBADD = 
libgstmixer_la_CFLAGS = $(GST_CFLAGS) $(GST_OPT_CFLAGS)
libgstmixer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
