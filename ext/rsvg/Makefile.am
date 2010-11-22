plugin_LTLIBRARIES = libgstrsvg.la

libgstrsvg_la_SOURCES = gstrsvg.c gstrsvgdec.c gstrsvgoverlay.c
libgstrsvg_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(RSVG_CFLAGS)
libgstrsvg_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(RSVG_LIBS)
libgstrsvg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(LIBM)
libgstrsvg_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstrsvgdec.h gstrsvgoverlay.h
