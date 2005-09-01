plugin_LTLIBRARIES = libgstfaac.la

libgstfaac_la_SOURCES = gstfaac.c
libgstfaac_la_CFLAGS = $(FAAC_CFLAGS) $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstfaac_la_LIBADD = $(FAAC_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_MAJORMINOR@
libgstfaac_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstfaac.h
