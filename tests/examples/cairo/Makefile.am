if USE_CAIRO_GOBJECT
if HAVE_GTK
noinst_PROGRAMS = cairo_overlay
endif
endif

cairo_overlay_SOURCES = cairo_overlay.c
cairo_overlay_CFLAGS = $(GST_CFLAGS) $(GTK_CFLAGS) $(CAIRO_CFLAGS)
cairo_overlay_LDFLAGS = \
	$(GST_LIBS) -lgstinterfaces-0.10 -lgstvideo-$(GST_MAJORMINOR) \
	$(GTK_LIBS) $(CAIRO_LIBS)
