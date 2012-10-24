plugin_LTLIBRARIES = libgstfbdevsink.la

libgstfbdevsink_la_SOURCES = gstfbdevsink.c
libgstfbdevsink_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(LIBFBDEV_CFLAGS)
libgstfbdevsink_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(LIBFBDEV_LIBS)
libgstfbdevsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfbdevsink_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstfbdevsink.h
