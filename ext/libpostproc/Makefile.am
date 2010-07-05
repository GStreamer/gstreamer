plugin_LTLIBRARIES = libgstpostproc.la

libgstpostproc_la_SOURCES = gstpostproc.c

libgstpostproc_la_CFLAGS = $(POSTPROC_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(ORC_CFLAGS)
libgstpostproc_la_LIBADD = -lgstvideo-@GST_MAJORMINOR@ \
	$(POSTPROC_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(ORC_LIBS)

libgstpostproc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DARWIN_LDFLAGS)
libgstpostproc_la_LIBTOOLFLAGS = --tag=disable-static

