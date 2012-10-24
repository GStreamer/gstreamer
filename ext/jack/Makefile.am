
plugin_LTLIBRARIES = libgstjack.la

libgstjack_la_SOURCES = gstjackutil.c gstjack.c gstjackaudiosrc.c gstjackaudiosink.c gstjackaudioclient.c
libgstjack_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(JACK_CFLAGS)
libgstjack_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(JACK_LIBS)
libgstjack_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstjack_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstjackutil.h gstjackaudiosrc.h gstjackaudiosink.h gstjackaudioclient.h gstjack.h gstjackringbuffer.h
