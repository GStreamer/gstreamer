noinst_HEADERS = gstaudiorate.h

plugin_LTLIBRARIES = libgstaudiorate.la

libgstaudiorate_la_SOURCES = gstaudiorate.c
libgstaudiorate_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstaudiorate_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudiorate_la_LIBADD = $(GST_LIBS) \
        $(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la

libgstaudiorate_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
