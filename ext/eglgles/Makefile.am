plugin_LTLIBRARIES = libgsteglglessink.la

libgsteglglessink_la_SOURCES = gsteglglessink.c video_platform_wrapper.c gstqueuearray.c gstdataqueue.c

libgsteglglessink_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
                              $(GST_BASE_CFLAGS) \
                              $(GST_CFLAGS) \
			      $(EGL_CFLAGS) \
                              $(EGLGLES_CFLAGS)

libgsteglglessink_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) $(EGL_LIBS) $(EGLGLES_LIBS) \
	-lgstvideo-$(GST_API_VERSION)

libgsteglglessink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsteglglessink_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gsteglglessink.h video_platform_wrapper.h gstqueuearray.h gstdataqueue.h
