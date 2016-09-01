plugin_LTLIBRARIES = libgstvulkan.la

SUBDIRS =
DIST_SUBDIRS = xcb wayland
DISTCLEANFILES = vkconfig.h

libgstvulkan_la_SOURCES = \
	gstvulkan.c \
	vkdevice.c \
	vkdisplay.c \
	vkerror.c \
	vkfence.c \
	vkbuffermemory.c \
	vkimagememory.c \
	vkbufferpool.c \
	vkinstance.c \
	vkmemory.c \
	vkqueue.c \
	vktrash.c \
	vksink.c \
	vkswapper.c \
	vkupload.c \
	vkutils.c \
	vkwindow.c

noinst_HEADERS = \
	vk.h \
	vkapi.h \
	vk_fwd.h \
	vkbuffermemory.h \
	vkbufferpool.h \
	vkconfig.h \
	vkdevice.h \
	vkdisplay.h \
	vkerror.h \
	vkfence.h \
	vkimagememory.h \
	vkinstance.h \
	vkmacros.h \
	vkmemory.h \
	vkqueue.h \
	vktrash.h \
	vksink.h \
	vkswapper.h \
	vkupload.h \
	vkutils.h \
	vkutils_private.h \
	vkwindow.h

libgstvulkan_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	-I$(top_builddir)/ext/vulkan \
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

if USE_WAYLAND
SUBDIRS += wayland
libgstvulkan_la_LIBADD += wayland/libgstvulkan-wayland.la
endif

libgstvulkan_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvulkan_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)


