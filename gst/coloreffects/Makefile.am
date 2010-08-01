plugin_LTLIBRARIES = libgstcoloreffects.la

libgstcoloreffects_la_SOURCES = \
	gstplugin.c \
	gstcoloreffects.c
libgstcoloreffects_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstcoloreffects_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstcoloreffects_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcoloreffects_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstcoloreffects.h
