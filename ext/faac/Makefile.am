plugin_LTLIBRARIES = libgstfaac.la

libgstfaac_la_SOURCES = gstfaac.c
libgstfaac_la_CFLAGS = -DGST_USE_UNSTABLE_API \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) $(FAAC_CFLAGS)
libgstfaac_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_MAJORMINOR@ -lgstpbutils-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) $(FAAC_LIBS)
libgstfaac_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfaac_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstfaac.h
