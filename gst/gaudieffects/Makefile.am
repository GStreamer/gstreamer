plugin_LTLIBRARIES = libgstgaudieffects.la

libgstgaudieffects_la_SOURCES = gstburn.c gstchromium.c gstdilate.c \
        gstdodge.c gstexclusion.c gstsolarize.c gstplugin.c
libgstgaudieffects_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstgaudieffects_la_LIBADD = $(GST_LIBS) -lgstvideo-@GST_MAJORMINOR@
libgstgaudieffects_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgaudieffects_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = \
        gstburn.h gstchromium.h gstdilate.h gstdodge.h \
        gstexclusion.h gstplugin.h gstsolarize.h
