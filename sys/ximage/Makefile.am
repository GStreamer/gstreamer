plugin_LTLIBRARIES = libgstximagesrc.la

libgstximagesrc_la_SOURCES = gstximagesrc.c ximageutil.c
libgstximagesrc_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(X_CFLAGS) $(XFIXES_CFLAGS) $(XDAMAGE_CFLAGS)
libgstximagesrc_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(X_LIBS) $(XSHM_LIBS) $(XFIXES_LIBS) $(XDAMAGE_LIBS) 
libgstximagesrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstximagesrc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstximagesrc.h ximageutil.h
