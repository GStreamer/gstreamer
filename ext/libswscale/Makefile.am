plugin_LTLIBRARIES = libgstavscale.la

libgstavscale_la_SOURCES = gstffmpegscale.c

libgstavscale_la_CFLAGS = $(SWSCALE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	$(ORC_CFLAGS)
libgstavscale_la_LIBADD = $(SWSCALE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(ORC_LIBS) $(LIBM) -lz
libgstavscale_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DARWIN_LDFLAGS)
libgstavscale_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

if HAVE_LIBAV_UNINSTALLED
libgstavscale_la_DEPENDENCIES = $(SWSCALE_DEPS)
endif
