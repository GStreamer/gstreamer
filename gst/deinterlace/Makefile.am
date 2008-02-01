plugin_LTLIBRARIES = libgstdeinterlace.la

libgstdeinterlace_la_SOURCES = gstdeinterlace.c
libgstdeinterlace_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS)
libgstdeinterlace_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) $(GST_BASE_LIBS)
libgstdeinterlace_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstdeinterlace.h 

