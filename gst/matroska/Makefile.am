plugin_LTLIBRARIES = libgstmatroska.la

libgstmatroska_la_SOURCES = \
	ebml-read.c \
	matroska.c \
	matroska-demux.c

noinst_HEADERS = \
	ebml-ids.h \
	ebml-read.h \
	matroska-demux.h \
	matroska-ids.h

libgstmatroska_la_CFLAGS = $(GST_CFLAGS)
libgstmatroska_la_LIBADD =
libgstmatroska_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
