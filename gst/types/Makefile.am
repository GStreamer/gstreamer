plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgsttypes.la

libgsttypes_la_SOURCES = gsttypes.c
libgsttypes_la_CFLAGS = $(GST_CFLAGS)
libgsttypes_la_LIBADD = $(GST_LIBS)
libgsttypes_la_LDFLAGS = -version-info $(GST_LIBVERSION)
