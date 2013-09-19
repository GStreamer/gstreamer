plugin_LTLIBRARIES = libgsteglglessink.la

if HAVE_IOS
DISTRO_SRC = gstegladaptation_eagl.m
else
DISTRO_SRC = gstegladaptation_egl.c video_platform_wrapper.c
endif

libgsteglglessink_la_SOURCES = gsteglglessink.c gstegladaptation.c $(DISTRO_SRC)

libgsteglglessink_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
			      $(GST_PLUGINS_BASE_CFLAGS) \
                              $(GST_BASE_CFLAGS) \
                              $(GST_CFLAGS) \
                              $(EGL_CFLAGS) \
                              $(EGLGLES_CFLAGS)
libgsteglglessink_la_OBJCFLAGS = $(GST_PLUGINS_BAD_OBJCFLAGS) \
                                 $(GST_PLUGINS_BASE_OBJCFLAGS) \
                                 $(GST_BASE_OBJCFLAGS) \
                                 $(GST_OBJCFLAGS) \
                                 $(EGLGLES_OBJCFLAGS) \
                                 -fobjc-abi-version=2 -fobjc-legacy-dispatch

libgsteglglessink_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) $(EGL_LIBS) $(EGLGLES_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	$(top_builddir)/gst-libs/gst/egl/libgstegl-$(GST_API_VERSION).la

if HAVE_IOS
libgsteglglessink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -Wl,-framework,OpenGLES -Wl,-framework,QuartzCore -Wl,-framework,IOKit -Wl,-framework,UIKit -Wl,-framework,CoreGraphics -Wl,-framework,CoreFoundation -Wl,-framework,Foundation -W
else
libgsteglglessink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
endif

libgsteglglessink_la_LIBTOOLFLAGS = --tag=CC $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gsteglglessink.h gstegladaptation.h video_platform_wrapper.h

EXTRA_DIST = gsteglglessink.c gstegladaptation.c gstegladaptation_egl.c video_platform_wrapper.c gstegladaptation_eagl.m $(noinst_HEADERS)
