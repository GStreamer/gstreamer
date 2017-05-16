plugin_LTLIBRARIES = libgstivtc.la

libgstivtc_la_SOURCES = \
	gstivtc.c gstivtc.h \
	gstcombdetect.c gstcombdetect.h
libgstivtc_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstivtc_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-1.0 \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstivtc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
