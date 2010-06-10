plugin_LTLIBRARIES = libgstvideomixer.la

ORC_SOURCE=blendorc
include $(top_srcdir)/common/orc.mak

libgstvideomixer_la_SOURCES = videomixer.c blend.c
nodist_libgstvideomixer_la_SOURCES = $(ORC_NODIST_SOURCES)
libgstvideomixer_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CONTROLLER_CFLAGS) $(GST_CFLAGS) $(ORC_CFLAGS)
libgstvideomixer_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) $(GST_CONTROLLER_LIBS) $(GST_LIBS) $(ORC_LIBS)
libgstvideomixer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideomixer_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = videomixer.h videomixerpad.h blend.h blend_mmx.h
