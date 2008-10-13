plugin_LTLIBRARIES = libgstmad.la 

libgstmad_la_SOURCES = gstmad.c gstid3tag.c

libgstmad_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(MAD_CFLAGS) $(ID3_CFLAGS)
libgstmad_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_MAJORMINOR) \
	-lgstaudio-$(GST_MAJORMINOR) $(MAD_LIBS) $(ID3_LIBS)
libgstmad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmad_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstmad.h
