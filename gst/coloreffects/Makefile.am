plugin_LTLIBRARIES = libgstcoloreffects.la

libgstcoloreffects_la_SOURCES = \
	gstplugin.c \
	gstcoloreffects.c \
	gstchromahold.c
libgstcoloreffects_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstcoloreffects_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstcoloreffects_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstcoloreffects.h gstchromahold.h
