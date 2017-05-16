plugin_LTLIBRARIES = libgstsiren.la

libgstsiren_la_SOURCES = gstsiren.c \
	gstsirendec.c gstsirendec.h \
	gstsirenenc.c gstsirenenc.h \
	common.c dct4.c decoder.c encoder.c huffman.c \
	huffman.h rmlt.h common.h dct4.h decoder.h \
	encoder.h huffman_consts.h rmlt.c siren7.h


libgstsiren_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstsiren_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstsiren_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
