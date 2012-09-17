plugin_LTLIBRARIES = libgstlame.la

libgstlame_la_SOURCES = gstlamemp3enc.c plugin.c
libgstlame_la_CFLAGS = \
        $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(LAME_CFLAGS)
libgstlame_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LAME_LIBS)
libgstlame_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstlame_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstlamemp3enc.h
