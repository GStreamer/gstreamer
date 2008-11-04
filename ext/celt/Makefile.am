plugin_LTLIBRARIES = libgstcelt.la

libgstcelt_la_SOURCES = gstcelt.c gstceltdec.c gstceltenc.c
libgstcelt_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(CELT_CFLAGS)
libgstcelt_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(CELT_LIBS)
libgstcelt_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(LIBM)
libgstcelt_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstceltenc.h gstceltdec.h 
