
plugin_LTLIBRARIES = libgstximagesrc.la

libgstximagesrc_la_SOURCES = ximagesrc.c ximageutil.c
libgstximagesrc_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(XFIXES_CFLAGS) $(XDAMAGE_CFLAGS)
libgstximagesrc_la_LIBADD = $(X_LIBS) $(XSHM_LIBS) \
	$(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) $(XFIXES_LIBS) $(XDAMAGE_LIBS)
libgstximagesrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = ximagesrc.h ximageutil.h
