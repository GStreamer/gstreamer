plugin_LTLIBRARIES = libgstinvtelecine.la

libgstinvtelecine_la_SOURCES = \
	gstinvtelecine.c

libgstinvtelecine_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)

libgstinvtelecine_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@

libgstinvtelecine_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstinvtelecine_la_LIBTOOLFLAGS = --tag=disable-static

