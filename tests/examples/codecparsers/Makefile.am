noinst_PROGRAMS = parse-jpeg

parse_jpeg_SOURCES = parse-jpeg.c
parse_jpeg_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_CFLAGS)
parse_jpeg_LDFLAGS = $(GST_LIBS)
parse_jpeg_LDADD = \
	$(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-$(GST_API_VERSION).la
