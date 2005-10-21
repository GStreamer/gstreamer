
plugin_LTLIBRARIES = libgstpostproc.la

libgstpostproc_la_SOURCES = gstpostproc.c

libgstpostproc_la_CFLAGS = $(GST_CFLAGS) \
	-I $(top_srcdir)/gst-libs/ext/ffmpeg/libavutil \
	-I $(top_srcdir)/gst-libs/ext/ffmpeg/libavcodec
libgstpostproc_la_LIBADD = \
	$(top_builddir)/gst-libs/ext/ffmpeg/libavcodec/libavcodec.la
#$(top_builddir)/gst-libs/ext/ffmpeg/libavutil/libavutil.la

libgstpostproc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstpostproc.h
