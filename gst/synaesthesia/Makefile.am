plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstsynaesthesia.la

libgstsynaesthesia_la_SOURCES = core.c gstsynaesthesia.c
libgstsynaesthesia_la_CFLAGS = $(GST_CFLAGS) $(GTK_CFLAGS)
libgstsynaesthesia_la_LIBADD = $(GST_LIBS) $(GTK_LIBS)
libgstsynaesthesia_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

noinst_HEADERS = core.h gstsynaesthesia.h

EXTRA_DIST = README README-syna
