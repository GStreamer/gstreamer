plugin_LTLIBRARIES = libgstdvbsuboverlay.la

libgstdvbsuboverlay_la_SOURCES = dvb-sub.c gstdvbsuboverlay.c

libgstdvbsuboverlay_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstdvbsuboverlay_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ $(GST_LIBS)
libgstdvbsuboverlay_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstdvbsuboverlay.h dvb-sub.h
