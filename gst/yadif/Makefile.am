plugin_LTLIBRARIES = libgstyadif.la

libgstyadif_la_SOURCES = gstyadif.c gstyadif.h vf_yadif.c yadif.c
libgstyadif_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstyadif_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-1.0 \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstyadif_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstyadif_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)


EXTRA_DIST = yadif_template.c
