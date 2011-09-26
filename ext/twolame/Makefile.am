plugin_LTLIBRARIES = libgsttwolame.la

libgsttwolame_la_SOURCES = gsttwolame.c
libgsttwolame_la_CFLAGS = -DGST_USE_UNSTABLE_API \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(TWOLAME_CFLAGS)
libgsttwolame_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_MAJORMINOR@ -lgstpbutils-@GST_MAJORMINOR@ \
	$(GST_LIBS) $(TWOLAME_LIBS)
libgsttwolame_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttwolame_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gsttwolame.h
