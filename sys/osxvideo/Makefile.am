
plugin_LTLIBRARIES = libgstosxvideo.la

libgstosxvideo_la_SOURCES = osxvideosink.m cocoawindow.m 
libgstosxvideo_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstosxvideo_la_OBJCFLAGS = $(GST_OBJCFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstosxvideo_la_LIBADD =  \
	$(GST_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION)

libgstosxvideo_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -Wl,-framework -Wl,Cocoa -Wl,-framework -Wl,OpenGL

noinst_HEADERS = osxvideosink.h cocoawindow.h
