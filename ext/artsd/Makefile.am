plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstartsdsink.la

libgstartsdsink_la_SOURCES = gstartsdsink.c
libgstartsdsink_la_CFLAGS = $(GST_CFLAGS) $(ARTSC_CFLAGS)
libgstartsdsink_la_LIBADD = $(ARTSC_LIBS)
libgstartsdsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstartsdsink.h
