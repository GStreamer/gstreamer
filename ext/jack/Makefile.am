plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstjack.la

libgstjack_la_SOURCES = gstjack.c
libgstjack_la_CFLAGS = $(GST_CFLAGS) $(JACK_CFLAGS)
libgstjack_la_LIBADD = $(JACK_LIBS)
libgstjack_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstjack.h

EXTRA_DIST = README
