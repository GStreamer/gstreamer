libgstinterfacesincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/tuner

libgstinterfacesinclude_HEADERS = \
		tuner.h \
		tunernorm.h \
		tunerchannel.h

noinst_LTLIBRARIES = libgsttuner.la

libgsttuner_la_SOURCES = \
	tuner.c \
	tunernorm.c \
	tunerchannel.c \
	tunermarshal.c
libgsttuner_la_CFLAGS = $(GST_CFLAGS)

BUILT_SOURCES = \
	tunermarshal.c \
	tunermarshal.h
built_headers =	\
	tunermarshal.h

EXTRA_DIST = tunermarshal.list

tunermarshal.h: tunermarshal.list
	glib-genmarshal --header --prefix=gst_tuner_marshal $^ > tunermarshal.h.tmp
	mv tunermarshal.h.tmp tunermarshal.h

tunermarshal.c: tunermarshal.list
	echo "#include \"tunermarshal.h\"" >> tunermarshal.c.tmp
	glib-genmarshal --body --prefix=gst_tuner_marshal $^ >> tunermarshal.c.tmp
	mv tunermarshal.c.tmp tunermarshal.c
