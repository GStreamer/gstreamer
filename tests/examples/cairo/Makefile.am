if USE_CAIRO_GOBJECT
if HAVE_GTK_X11
noinst_PROGRAMS = cairo_overlay
endif
endif

cairo_overlay_SOURCES = cairo_overlay.c
cairo_overlay_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(GTK_CFLAGS) $(CAIRO_CFLAGS)
cairo_overlay_LDFLAGS = \
	$(GST_LIBS) -lgstinterfaces-$(GST_MAJORMINOR) -lgstvideo-$(GST_MAJORMINOR) \
	$(GTK_LIBS) $(CAIRO_LIBS)
