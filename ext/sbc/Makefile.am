
plugin_LTLIBRARIES = libgstsbc.la

noinst_HEADERS = \
	gstsbcdec.h \
	gstsbcenc.h

libgstsbc_la_SOURCES = \
	gstsbcdec.c \
	gstsbcenc.c \
	sbc-plugin.c
libgstsbc_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(SBC_CFLAGS)
libgstsbc_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) \
	$(SBC_LIBS)
libgstsbc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
