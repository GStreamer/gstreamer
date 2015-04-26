plugin_LTLIBRARIES = libgstmad.la 

libgstmad_la_SOURCES = gstmad.c

libgstmad_la_CFLAGS = \
        $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	$(MAD_CFLAGS)
libgstmad_la_LIBADD = \
        $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(MAD_LIBS)
libgstmad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmad_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstmad.h
