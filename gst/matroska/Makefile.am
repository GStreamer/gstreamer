plugin_LTLIBRARIES = libgstmatroska.la

libgstmatroska_la_SOURCES = \
	ebml-read.c \
	ebml-write.c \
	matroska.c \
	matroska-demux.c \
	matroska-ids.c \
	matroska-mux.c

noinst_HEADERS = \
	ebml-ids.h \
	ebml-read.h \
	ebml-write.h \
	matroska-demux.h \
	matroska-ids.h \
	matroska-mux.h

libgstmatroska_la_CFLAGS = \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	-I$(top_srcdir)/gst-libs
libgstmatroska_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_LIBS) \
	-lgstriff-@GST_MAJORMINOR@ \
	-lgsttag-@GST_MAJORMINOR@ \
	$(ZLIB_LIBS) \
	$(LIBM)
libgstmatroska_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
