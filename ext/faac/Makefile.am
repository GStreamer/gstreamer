plugin_LTLIBRARIES = libgstfaac.la

libgstfaac_la_SOURCES = gstfaac.c
libgstfaac_la_CFLAGS = $(FAAC_CFLAGS) $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstfaac_la_LIBADD = $(FAAC_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_MAJORMINOR@ -lgstpbutils-@GST_MAJORMINOR@
libgstfaac_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfaac_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstfaac.h
