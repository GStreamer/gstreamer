libgstinterfacesincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/mixer

libgstinterfacesinclude_HEADERS = \
	mixer.h \
	mixertrack.h

noinst_LTLIBRARIES = libgstmixer.la

libgstmixer_la_SOURCES = \
	mixer.c \
	mixertrack.c
libgstmixer_la_CFLAGS = $(GST_CFLAGS)
