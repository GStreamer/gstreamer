GST_WAYLANDSINK_UI_FILES = window.ui
if HAVE_GTK3_WAYLAND

GST_WAYLANDSINK_GTK_EXAMPLE = gtkwaylandsink
gtkwaylandsink_SOURCES = main.c

gtkwaylandsink_CFLAGS=-I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GTK3_CFLAGS)
gtkwaylandsink_LDADD=$(GTK3_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	$(top_builddir)/gst-libs/gst/wayland/libgstwayland-$(GST_API_VERSION).la
else
GST_WAYLANDSINK_GTK_EXAMPLE =
endif

wayland_threads_SOURCES = wayland-threads.c
wayland_threads_CFLAGS=-I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(WAYLAND_CFLAGS)
wayland_threads_LDADD=$(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) $(WAYLAND_LIBS)\
	-lgstvideo-$(GST_API_VERSION) \
	$(top_builddir)/gst-libs/gst/wayland/libgstwayland-$(GST_API_VERSION).la

noinst_PROGRAMS = $(GST_WAYLANDSINK_GTK_EXAMPLE) wayland_threads

EXTRA_DIST = $(GST_WAYLANDSINK_UI_FILES)
