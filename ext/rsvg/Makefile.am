plugin_LTLIBRARIES = libgstrsvg.la

libgstrsvg_la_SOURCES = gstrsvg.c gstrsvgdec.c gstrsvgoverlay.c
libgstrsvg_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(RSVG_CFLAGS)
libgstrsvg_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(RSVG_LIBS)
libgstrsvg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(LIBM)

noinst_HEADERS = gstrsvgdec.h gstrsvgoverlay.h
