## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-wayland.la

xdg_shell_protocol_spec = $(WAYLAND_PROTOCOLS_DATADIR)/stable/xdg-shell/xdg-shell.xml
xdg-shell-client-protocol.h: $(xdg_shell_protocol_spec)
	$(AM_V_GEN) $(WAYLAND_SCANNER) client-header < $< > $@
xdg-shell-client-protocol.c: $(xdg_shell_protocol_spec)
	$(AM_V_GEN) $(WAYLAND_SCANNER) private-code < $< > $@

built_sources = xdg-shell-client-protocol.c
built_headers = xdg-shell-client-protocol.h

BUILT_SOURCES = $(built_sources) $(built_headers)
CLEANFILES = $(BUILT_SOURCES)

libgstgl_wayland_la_SOURCES = \
	gstgldisplay_wayland.c \
	gstglwindow_wayland_egl.c \
	wayland_event_source.c \
	$(built_sources)

noinst_HEADERS = \
	gstgldisplay_wayland_private.h \
	gstglwindow_wayland_egl.h \
	wayland_event_source.h \
	$(built_headers)

libgstgl_waylandincludedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl/wayland
libgstgl_waylandinclude_HEADERS = \
	gstgldisplay_wayland.h

libgstgl_wayland_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	-DBUILDING_GST_GL \
	$(GL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstgl_wayland_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
