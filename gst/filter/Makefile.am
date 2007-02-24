
plugin_LTLIBRARIES = libgstfilter.la

libgstfilter_la_SOURCES = gstfilter.c gstlpwsinc.c gstbpwsinc.c gstiir.c iir.c
libgstfilter_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GST_CONTROLLER_CFLAGS)
libgstfilter_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(GST_CONTROLLER_LIBS) \
	$(LIBM)

libgstfilter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstfilter.h gstlpwsinc.h gstbpwsinc.h gstiir.h iir.h
