noinst_PROGRAMS = tsparser

tsparser_SOURCES = ts-parser.c
tsparser_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_CFLAGS)
tsparser_LDFLAGS = $(GST_LIBS)
tsparser_LDADD = \
	$(top_builddir)/gst-libs/gst/mpegts/libgstmpegts-$(GST_API_VERSION).la
