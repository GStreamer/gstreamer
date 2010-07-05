plugin_LTLIBRARIES = libgstffmpegscale.la

libgstffmpegscale_la_SOURCES = gstffmpegscale.c

libgstffmpegscale_la_CFLAGS = $(SWSCALE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	$(ORC_CFLAGS)
libgstffmpegscale_la_LIBADD = $(SWSCALE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(ORC_LIBS) $(LIBM) -lz
libgstffmpegscale_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DARWIN_LDFLAGS)
libgstffmpegscale_la_LIBTOOLFLAGS = --tag=disable-static

if HAVE_FFMPEG_UNINSTALLED
libgstffmpegscale_la_DEPENDENCIES = $(SWSCALE_LIBS)
endif
