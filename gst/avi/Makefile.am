plugindir = $(libdir)/gst

plugin_LTLIBRARIES = \
		libgstavimux.la \
		libgstavidemux.la
#		libgstaviaudiodecoder.la

libgstavidemux_la_SOURCES = gstavidemux.c 
libgstavimux_la_SOURCES = gstavimux.c 
# libgstaviaudiodecoder_la_SOURCES = gstaviaudiodecoder.c 

noinst_HEADERS = \
		gstavimux.h \
		gstavidemux.h \
		gstaviaudiodecoder.h

#CFLAGS += -Wall -O3 -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math -DNDEBUG

libgstavidemux_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS)
libgstavidemux_la_LIBADD =
libgstavidemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstavimux_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS)
libgstavimux_la_LIBADD =
libgstavimux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# libgstaviaudiodecoder_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS)
# libgstaviaudiodecoder_la_LIBADD =
# libgstaviaudiodecoder_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

EXTRA_DIST = README_win32dll README
