noinst_PROGRAMS = cairo_overlay

cairo_overlay_SOURCES = cairo_overlay.c
cairo_overlay_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(CAIRO_CFLAGS)
cairo_overlay_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) $(GST_LIBS) $(CAIRO_LIBS)
