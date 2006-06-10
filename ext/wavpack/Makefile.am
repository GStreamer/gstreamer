
plugin_LTLIBRARIES = libgstwavpack.la

libgstwavpack_la_SOURCES = \
				gstwavpack.c \
				gstwavpackcommon.c \
				gstwavpackparse.c \
				gstwavpackdec.c \
				gstwavpackenc.c \
				md5.c

libgstwavpack_la_CFLAGS = $(GST_CFLAGS) $(WAVPACK_CFLAGS)
libgstwavpack_la_LIBADD = $(GST_LIBS) $(WAVPACK_LIBS)
libgstwavpack_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
		gstwavpackparse.h \
		gstwavpackdec.h \
		gstwavpackenc.h \
		gstwavpackcommon.h \
		md5.h

