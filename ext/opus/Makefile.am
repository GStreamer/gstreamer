plugin_LTLIBRARIES = libgstopus.la

libgstopus_la_SOURCES = gstopus.c gstopusdec.c gstopusenc.c
libgstopus_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(OPUS_CFLAGS)
libgstopus_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(OPUS_LIBS)
libgstopus_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(LIBM)
libgstopus_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstopusenc.h gstopusdec.h 
