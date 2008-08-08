plugin_LTLIBRARIES = libgstx264.la

libgstx264_la_SOURCES = gstx264enc.c
libgstx264_la_CFLAGS = $(X264_CFLAGS) $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstx264_la_LIBADD = $(X264_LIBS) $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) \
			-lgstvideo-$(GST_MAJORMINOR)
libgstx264_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstx264enc.h
