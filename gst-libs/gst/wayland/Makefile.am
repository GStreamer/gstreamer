lib_LTLIBRARIES = libgstwayland-@GST_API_VERSION@.la

libgstwayland_@GST_API_VERSION@_la_SOURCES = wayland.c

libgstwayland_@GST_API_VERSION@includedir = \
	$(includedir)/gstreamer-@GST_API_VERSION@/gst/wayland

libgstwayland_@GST_API_VERSION@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(WAYLAND_CFLAGS)

libgstwayland_@GST_API_VERSION@_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_LIBS) \
	$(WAYLAND_LIBS) \
	-lgstvideo-$(GST_API_VERSION)

libgstwayland_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)

noinst_HEADERS = \
	wayland.h

