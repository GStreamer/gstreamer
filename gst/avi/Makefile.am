plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstavimux.la libgstavidecoder.la libgstavidemux.la libgstavitypes.la libgstaviaudiodecoder.la

libgstavidemux_la_SOURCES = gstavidemux.c 
libgstavitypes_la_SOURCES = gstavitypes.c 
libgstavidecoder_la_SOURCES = gstavidecoder.c 
libgstavimux_la_SOURCES = gstavimux.c 
libgstaviaudiodecoder_la_SOURCES = gstaviaudiodecoder.c 

noinst_HEADERS = gstavidecoder.h gstavimux.h gstavitypes.h gstavidemux.h gstaviaudiodecoder.h

#CFLAGS += -Wall -O3 -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math -DNDEBUG
libgstavidecoder_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS)
libgstavidecoder_la_LIBADD = $(GST_LIBS)
libgstavidemux_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS)
libgstavidemux_la_LIBADD = $(GST_LIBS)
libgstavitypes_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS)
libgstavitypes_la_LIBADD = $(GST_LIBS)
libgstavimux_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS)
libgstavimux_la_LIBADD = $(GST_LIBS)
libgstaviaudiodecoder_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS)
libgstaviaudiodecoder_la_LIBADD = $(GST_LIBS)

EXTRA_DIST = README_win32dll README
