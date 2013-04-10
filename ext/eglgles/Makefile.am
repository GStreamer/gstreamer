plugin_LTLIBRARIES = libgsteglglessink.la

libgsteglglessink_la_SOURCES = gsteglglessink.c gstegladaptation.c video_platform_wrapper.c

libgsteglglessink_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
			      $(GST_PLUGINS_BASE_CFLAGS) \
                              $(GST_BASE_CFLAGS) \
                              $(GST_CFLAGS) \
			      $(EGL_CFLAGS) \
                              $(EGLGLES_CFLAGS)

libgsteglglessink_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) $(EGL_LIBS) $(EGLGLES_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	$(top_builddir)/gst-libs/gst/egl/libgstegl-$(GST_API_VERSION).la

libgsteglglessink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsteglglessink_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gsteglglessink.h gstegladaptation.h video_platform_wrapper.h
