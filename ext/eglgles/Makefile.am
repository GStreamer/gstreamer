plugin_LTLIBRARIES = libgsteglglessink.la

libgsteglglessink_la_SOURCES = gsteglglessink.c video_platform_wrapper.c

libgsteglglessink_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
                              $(GST_CFLAGS) \
                              $(EGLGLES_CFLAGS) \
                              $(X11_CFLAGS)

libgsteglglessink_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) $(EGLGLES_LIBS) $(X11_LIBS) \
	-lgstvideo-$(GST_MAJORMINOR) \
	-lgstinterfaces-$(GST_MAJORMINOR)

libgsteglglessink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsteglglessink_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gsteglglessink.h video_platform_wrapper.h
