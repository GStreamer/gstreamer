if USE_CAIRO_GOBJECT
noinst_PROGRAMS = cairo_overlay
endif

cairo_overlay_SOURCES = cairo_overlay.c
cairo_overlay_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(CAIRO_CFLAGS)
cairo_overlay_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) $(GST_LIBS) $(CAIRO_LIBS)
