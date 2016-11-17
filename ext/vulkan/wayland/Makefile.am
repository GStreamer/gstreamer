## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstvulkan-wayland.la

libgstvulkan_wayland_la_SOURCES = \
	vkdisplay_wayland.c \
	vkwindow_wayland.c \
	wayland_event_source.c

noinst_HEADERS = \
	vkdisplay_wayland.h \
	vkwindow_wayland.h \
	wayland_event_source.h

libgstvulkan_wayland_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_srcdir)/ext/vulkan \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(VULKAN_WAYLAND_CFLAGS)

libgstvulkan_wayland_la_LIBADD = \
	$(VULKAN_WAYLAND_LIBS)

libgstvulkan_wayland_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
