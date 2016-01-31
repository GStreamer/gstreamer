plugin_LTLIBRARIES = libgstfaad.la

libgstfaad_la_SOURCES = gstfaad.c
libgstfaad_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(FAAD_CFLAGS)
libgstfaad_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(FAAD_LIBS) $(LIBM)
libgstfaad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfaad_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstfaad.h
