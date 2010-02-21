plugin_LTLIBRARIES = libgstfaad.la

libgstfaad_la_SOURCES = gstfaad.c
libgstfaad_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(FAAD_CFLAGS)
libgstfaad_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(FAAD_LIBS)
libgstfaad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfaad_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstfaad.h
