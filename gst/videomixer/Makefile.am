plugin_LTLIBRARIES = libgstvideomixer.la

libgstvideomixer_la_SOURCES = videomixer.c blend.c
libgstvideomixer_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CONTROLLER_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(LIBOIL_CFLAGS)
libgstvideomixer_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_CONTROLLER_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ $(LIBOIL_LIBS)
libgstvideomixer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideomixer_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = videomixer.h videomixerpad.h blend.h blend_mmx.h
