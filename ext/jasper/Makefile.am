plugin_LTLIBRARIES = libgstjasper.la

libgstjasper_la_SOURCES = gstjasperdec.c gstjasperenc.c gstjasper.c
libgstjasper_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(JASPER_CFLAGS)
libgstjasper_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_LIBS) $(JASPER_LIBS)
libgstjasper_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstjasper_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstjasperdec.h \
	gstjasperenc.h
