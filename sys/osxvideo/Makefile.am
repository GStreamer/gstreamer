
plugin_LTLIBRARIES = libgstosxvideosink.la

libgstosxvideosink_la_SOURCES = osxvideosink.m cocoawindow.m 
libgstosxvideosink_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstosxvideosink_la_OBJCFLAGS = $(GST_OBJCFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstosxvideosink_la_LIBADD =  \
	$(GST_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION)

libgstosxvideosink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -Wl,-framework -Wl,Cocoa -Wl,-framework -Wl,OpenGL
libgstosxvideosink_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS) --tag=CC

noinst_HEADERS = osxvideosink.h cocoawindow.h
