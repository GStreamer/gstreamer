plugin_LTLIBRARIES = libgstcairo.la

noinst_HEADERS = \
	gstcairooverlay.h
libgstcairo_la_SOURCES = \
	gstcairo.c \
	gstcairooverlay.c
libgstcairo_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) $(CAIRO_CFLAGS)
libgstcairo_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(CAIRO_LIBS) $(LIBM)
libgstcairo_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcairo_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

