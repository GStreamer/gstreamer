plugin_LTLIBRARIES = libgstwavpack.la

libgstwavpack_la_SOURCES = \
				gstwavpack.c \
				gstwavpackcommon.c \
				gstwavpackparse.c \
				gstwavpackdec.c \
				gstwavpackenc.c \
				gstwavpackstreamreader.c

libgstwavpack_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
				$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(WAVPACK_CFLAGS)
libgstwavpack_la_LIBADD =  $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR) \
				$(GST_BASE_LIBS) $(GST_LIBS) $(WAVPACK_LIBS)
libgstwavpack_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstwavpack_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
		gstwavpackparse.h \
		gstwavpackdec.h \
		gstwavpackenc.h \
		gstwavpackcommon.h \
		gstwavpackstreamreader.h

