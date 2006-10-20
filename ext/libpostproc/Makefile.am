plugin_LTLIBRARIES = libgstpostproc.la

libgstpostproc_la_SOURCES = gstpostproc.c

libgstpostproc_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(LIBOIL_CFLAGS) \
	-I $(top_srcdir)/gst-libs/ext/ffmpeg \
	-I $(top_srcdir)/gst-libs/ext/ffmpeg/libavformat \
	-I $(top_srcdir)/gst-libs/ext/ffmpeg/libavutil \
	-I $(top_srcdir)/gst-libs/ext/ffmpeg/libavcodec
libgstpostproc_la_LIBADD = -lgstvideo-@GST_MAJORMINOR@ \
	$(top_builddir)/gst-libs/ext/ffmpeg/libavcodec/libavcodec.la \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(LIBOIL_LIBS)

libgstpostproc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
