
plugin_LTLIBRARIES = libgstspeed.la

libgstspeed_la_SOURCES = gstspeed.c
libgstspeed_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstspeed_la_LIBADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) $(LIBM) -lgstaudio-$(GST_API_VERSION)
libgstspeed_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstspeed.h
