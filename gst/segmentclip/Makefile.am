plugin_LTLIBRARIES = libgstsegmentclip.la

libgstsegmentclip_la_SOURCES = gstsegmentclip.c gstaudiosegmentclip.c gstvideosegmentclip.c plugin.c

libgstsegmentclip_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstsegmentclip_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) -lgstaudio-@GST_MAJORMINOR@
libgstsegmentclip_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsegmentclip_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstsegmentclip.h gstaudiosegmentclip.h gstvideosegmentclip.h

