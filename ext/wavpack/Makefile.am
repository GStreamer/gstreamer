
plugin_LTLIBRARIES = libgstwavpack.la

libgstwavpack_la_SOURCES = gstwavpack.c \
													 gstwavpackcommon.c \
													 gstwavpackparse.c \
													 gstwavpackdec.c

libgstwavpack_la_CFLAGS = $(GST_CFLAGS) $(WAVPACK_CFLAGS)
libgstwavpack_la_LIBADD = $(WAVPACK_LIBS)
libgstwavpack_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstwavpackparse.h gstwavpackdec.h gstwavpackcommon.h
