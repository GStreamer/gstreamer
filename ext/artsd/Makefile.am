plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libartsdsink.la

libartsdsink_la_SOURCES = gstartsdsink.c
libartsdsink_la_CFLAGS = $(GST_CFLAGS) $(ARTSC_CFLAGS)
libartsdsink_la_LIBADD = $(GST_LIBS) $(ARTSC_LIBS)
libartsdsink_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

noinst_HEADERS = gstartsdsink.h
