if USE_CAIRO_GOBJECT
noinst_PROGRAMS = cairo_overlay
endif

cairo_overlay_SOURCES = cairo_overlay.c
cairo_overlay_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(CAIRO_CFLAGS)
cairo_overlay_LDFLAGS = -lgstvideo-$(GST_MAJORMINOR) $(CAIRO_LIBS)
