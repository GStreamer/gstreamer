libgstinterfacesincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/tuner

libgstinterfacesinclude_HEADERS = \
		tuner.h \
		tunernorm.h \
		tunerchannel.h

lib_LTLIBRARIES = libgsttuner.la

libgsttuner_la_SOURCES = \
	tuner.c \
	tunernorm.c \
	tunerchannel.c
libgsttuner_la_LIBADD = 
libgsttuner_la_CFLAGS = $(GST_CFLAGS) $(GST_OPT_CFLAGS)
libgsttuner_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
