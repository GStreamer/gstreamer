plugin_LTLIBRARIES = libgstcairo.la

noinst_HEADERS = gsttimeoverlay.h gsttextoverlay.h gstcairorender.h

libgstcairo_la_SOURCES = \
	gstcairo.c \
	gsttimeoverlay.c \
	gsttextoverlay.c \
	gstcairorender.c

libgstcairo_la_CFLAGS = \
        $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) $(CAIRO_CFLAGS)
libgstcairo_la_LIBADD = \
  $(GST_BASE_LIBS) \
  $(GST_LIBS) $(CAIRO_LIBS) $(LIBM)
libgstcairo_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcairo_la_LIBTOOLFLAGS = --tag=disable-static

