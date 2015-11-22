noinst_PROGRAMS = gtkwaylandsink

gtkwaylandsink_SOURCES = main.c

gtkwaylandsink_CFLAGS=-I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GTK3_CFLAGS)
gtkwaylandsink_LDADD=$(GTK3_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	$(top_builddir)/gst-libs/gst/wayland/libgstwayland-$(GST_API_VERSION).la

EXTRA_DIST = window.ui
