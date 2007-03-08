
plugin_LTLIBRARIES = libgstjack.la

libgstjack_la_SOURCES = gstjack.c gstjackaudiosink.c gstjackaudioclient.c
libgstjack_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(JACK_CFLAGS)
libgstjack_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR) $(JACK_LIBS)
libgstjack_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstjackaudiosink.h gstjackaudioclient.h

EXTRA_DIST = README
