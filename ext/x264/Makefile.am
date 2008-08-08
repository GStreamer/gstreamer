plugin_LTLIBRARIES = libgstx264.la

libgstx264_la_SOURCES = gstx264enc.c
libgstx264_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(X264_CFLAGS)
libgstx264_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_LIBS) \
	$(X264_LIBS)
libgstx264_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstx264enc.h
