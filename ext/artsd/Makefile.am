plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libartsdsink.la

libartsdsink_la_SOURCES = gstartsdsink.c
noinst_HEADERS = gstartsdsink.h

libartsdsink_la_CFLAGS = $(ARTSC_CFLAGS) $(GST_CFLAGS)
libartsdsink_la_LIBADD = $(ARTSC_LIBS)
