plugin_LTLIBRARIES = libgstjp2k.la

libgstjp2k_la_SOURCES = gstjasperdec.c gstjasperenc.c gstjp2k.c
libgstjp2k_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(JP2K_CFLAGS)
libgstjp2k_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_LIBS) $(JP2K_LIBS)
libgstjp2k_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstjp2k_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstjasperdec.h \
	gstjasperenc.h