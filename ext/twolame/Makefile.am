plugin_LTLIBRARIES = libgsttwolame.la

libgsttwolame_la_SOURCES = gsttwolamemp2enc.c
libgsttwolame_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(TWOLAME_CFLAGS)
libgsttwolame_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_API_VERSION@ -lgstpbutils-@GST_API_VERSION@ \
	$(GST_LIBS) $(TWOLAME_LIBS)
libgsttwolame_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttwolame_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gsttwolamemp2enc.h
