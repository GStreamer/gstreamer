plugin_LTLIBRARIES = libgstcairo.la

# we probably don't really want to port these
#	gsttimeoverlay.h
#	gsttextoverlay.h
#	gstcairorender.h
#	gsttimeoverlay.c
#	gsttextoverlay.c
#	gstcairorender.c

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
libgstcairo_la_LIBTOOLFLAGS = --tag=disable-static

