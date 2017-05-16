plugin_LTLIBRARIES = libgstfaac.la

libgstfaac_la_SOURCES = gstfaac.c
libgstfaac_la_CFLAGS = -DGST_USE_UNSTABLE_API \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) $(FAAC_CFLAGS)
libgstfaac_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_API_VERSION@ -lgstpbutils-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(FAAC_LIBS)
libgstfaac_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstfaac.h
