libgstinterfacesincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/mixer

libgstinterfacesinclude_HEADERS = \
	mixer.h \
	mixertrack.h

noinst_LTLIBRARIES = libgstmixer.la

libgstmixer_la_SOURCES = \
	mixer.c \
	mixertrack.c \
	mixermarshal.c
libgstmixer_la_CFLAGS = $(GST_CFLAGS)

BUILT_SOURCES = \
	mixermarshal.c \
	mixermarshal.h
built_headers =	\
	mixermarshal.h

EXTRA_DIST = mixermarshal.list

mixermarshal.h: mixermarshal.list
	glib-genmarshal --header --prefix=gst_mixer_marshal $^ > mixermarshal.h.tmp
	mv mixermarshal.h.tmp mixermarshal.h

mixermarshal.c: mixermarshal.list
	echo "#include \"mixermarshal.h\"" >> mixermarshal.c.tmp
	glib-genmarshal --body --prefix=gst_mixer_marshal $^ >> mixermarshal.c.tmp
	mv mixermarshal.c.tmp mixermarshal.c
