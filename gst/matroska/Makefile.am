plugin_LTLIBRARIES = libgstmatroska.la

libgstmatroska_la_SOURCES = \
	ebml-read.c \
	ebml-write.c \
	matroska.c \
	matroska-demux.c \
	matroska-ids.c \
	matroska-mux.c \
	lzo.c

noinst_HEADERS = \
	ebml-ids.h \
	ebml-read.h \
	ebml-write.h \
	matroska-demux.h \
	matroska-ids.h \
	matroska-mux.h \
	lzo.h

libgstmatroska_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstmatroska_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstriff-@GST_MAJORMINOR@ \
	-lgstaudio-@GST_MAJORMINOR@ \
	-lgsttag-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(ZLIB_LIBS) \
	$(BZ2_LIBS) \
	$(LIBM)
libgstmatroska_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmatroska_la_LIBTOOLFLAGS = --tag=disable-static

