plugin_LTLIBRARIES = libgstvideomixer.la

libgstvideomixer_la_SOURCES = videomixer.c blend_ayuv.c blend_bgra.c blend_i420.c
libgstvideomixer_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CONTROLLER_CFLAGS)  $(GST_PLUGINS_BASE_CFLAGS) $(CAIRO_CFLAGS)
libgstvideomixer_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_CONTROLLER_LIBS) $(CAIRO_LIBS)
libgstvideomixer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideomixer_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = videomixer.h videomixerpad.h
