## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstvulkan-xcb.la

libgstvulkan_xcb_la_SOURCES = \
	vkdisplay_xcb.c \
	vkwindow_xcb.c \
	xcb_event_source.c

noinst_HEADERS = \
	vkdisplay_xcb.h \
	vkwindow_xcb.h \
	xcb_event_source.h

libgstvulkan_xcb_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/ext/vulkan \
	-I$(top_srcdir)/ext/vulkan \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(XCB_CFLAGS)

libgstvulkan_xcb_la_LIBADD = \
	$(XCB_LIBS)

libgstvulkan_xcb_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
