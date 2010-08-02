plugin_LTLIBRARIES = libgstgaudieffects.la

libgstgaudieffects_la_SOURCES = gstburn.c gstchromium.c gstdilate.c \
        gstdodge.c gstexclusion.c gstgaussblur.c gstsolarize.c gstplugin.c
libgstgaudieffects_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstgaudieffects_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ $(GST_CONTROLLER_LIBS) $(GST_LIBS) $(LIBM)
libgstgaudieffects_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgaudieffects_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = \
        gstburn.h gstchromium.h gstdilate.h gstdodge.h \
        gstexclusion.h gstgaussblur.h gstplugin.h gstsolarize.h
