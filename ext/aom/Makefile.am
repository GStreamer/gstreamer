plugin_LTLIBRARIES = libgstaom.la

libgstaom_la_SOURCES = \
	gstaom.c \
	gstav1enc.c \
	gstav1dec.c

libgstaom_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AOM_CFLAGS)
libgstaom_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) -lgstvideo-$(GST_API_VERSION) $(AOM_LIBS)
libgstaom_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstaom.h \
	gstav1dec.h \
	gstav1enc.h
