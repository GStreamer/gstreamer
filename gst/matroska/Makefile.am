plugin_LTLIBRARIES = libgstmatroska.la

libgstmatroska_la_SOURCES = \
	ebml-read.c \
	ebml-write.c \
	matroska.c \
	matroska-demux.c \
	matroska-parse.c \
	matroska-ids.c \
	matroska-mux.c \
	matroska-read-common.c \
	webm-mux.c \
	lzo.c

noinst_HEADERS = \
	ebml-ids.h \
	ebml-read.h \
	ebml-write.h \
	matroska-demux.h \
	matroska-parse.h \
	matroska-ids.h \
	matroska-mux.h \
	matroska-read-common.h \
	webm-mux.h \
	lzo.h

libgstmatroska_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstmatroska_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstriff-@GST_API_VERSION@ \
	-lgstaudio-@GST_API_VERSION@ \
	-lgstvideo-@GST_API_VERSION@ \
	-lgsttag-@GST_API_VERSION@ \
	-lgstpbutils-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(ZLIB_LIBS) \
	$(BZ2_LIBS) \
	$(LIBM)
libgstmatroska_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmatroska_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
