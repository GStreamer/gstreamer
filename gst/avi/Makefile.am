plugindir = $(libdir)/gst

plugin_LTLIBRARIES = \
		libgstavimux.la \
		libgstavidemux.la \
		libgstaviaudiodecoder.la \
		libgstcdxaparse.la

libgstavidemux_la_SOURCES = gstavidemux.c 
libgstavimux_la_SOURCES = gstavimux.c 
libgstaviaudiodecoder_la_SOURCES = gstaviaudiodecoder.c 
libgstcdxaparse_la_SOURCES = gstcdxaparse.c 

noinst_HEADERS = \
		gstavimux.h \
		gstavidemux.h \
		gstaviaudiodecoder.h \
		gstcdxaparse.h

#CFLAGS += -Wall -O3 -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math -DNDEBUG

libgstavidemux_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS)
libgstavidemux_la_LIBADD = $(GST_LIBS)
libgstavidemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstavimux_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS)
libgstavimux_la_LIBADD = $(GST_LIBS)
libgstavimux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstaviaudiodecoder_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS)
libgstaviaudiodecoder_la_LIBADD = $(GST_LIBS)
libgstaviaudiodecoder_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstcdxaparse_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS)
libgstcdxaparse_la_LIBADD = $(GST_LIBS)
libgstcdxaparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

EXTRA_DIST = README_win32dll README
