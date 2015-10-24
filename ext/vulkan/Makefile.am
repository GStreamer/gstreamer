plugin_LTLIBRARIES = libgstvulkan.la

SUBDIRS =
DIST_SUBDIRS = xcb
DISTCLEANFILES = vkconfig.h

libgstvulkan_la_SOURCES = \
	gstvulkan.c \
	vkdevice.c \
	vkdisplay.c \
	vkerror.c \
	vkimagememory.c \
	vkinstance.c \
	vkmemory.c \
	vkqueue.c \
	vksink.c \
	vkswapper.c \
	vkutils.c \
	vkwindow.c

noinst_HEADERS = \
	vk.h \
	vk_fwd.h \
	vkdevice.h \
	vkdisplay.h \
	vkerror.h \
	vkimagememory.h \
	vkinstance.h \
	vkmemory.h \
	vkqueue.h \
	vksink.h \
	vkswapper.h \
	vkutils.h \
	vkwindow.h

libgstvulkan_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(VULKAN_CFLAGS)

libgstvulkan_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	$(VULKAN_LIBS)

if USE_XCB
SUBDIRS += xcb
libgstvulkan_la_LIBADD += xcb/libgstvulkan-xcb.la
endif

libgstvulkan_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvulkan_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)


