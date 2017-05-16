plugin_LTLIBRARIES = libgstinterlace.la

libgstinterlace_la_SOURCES = \
	gstinterlace.c

libgstinterlace_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)

libgstinterlace_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	$(LIBM)

libgstinterlace_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
