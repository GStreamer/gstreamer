plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = \
		libgstavimux.la \
		libgstavidemux.la 

libgstavidemux_la_SOURCES = gstavidemux.c
libgstavimux_la_SOURCES = gstavimux.c 

noinst_HEADERS = \
		gstavimux.h \
		gstavidemux.h

#CFLAGS += $(GST_CFLAGS) -O3 $(GST_OPT_CFLAGS) $(GST_FUNROLL_CFLAGS) -fomit-frame-pointer -DNDEBUG

libgstavidemux_la_CFLAGS = -O2 $(GST_FFASTMATH_CFLAGS) $(GST_CFLAGS)
libgstavidemux_la_LIBADD =
libgstavidemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstavimux_la_CFLAGS = -O2 $(GST_FFASTMATH_CFLAGS) $(GST_CFLAGS)
libgstavimux_la_LIBADD =
libgstavimux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

EXTRA_DIST = README
