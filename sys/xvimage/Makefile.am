plugin_LTLIBRARIES = libgstxvimagesink.la

libgstxvimagesink_la_SOURCES =  xvimagesink.c 
libgstxvimagesink_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(X_CFLAGS)
libgstxvimagesink_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_MAJORMINOR).la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(X_LIBS) $(XVIDEO_LIBS) $(XSHM_LIBS) $(LIBM)
libgstxvimagesink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstxvimagesink_la_DEPENDENCIES = $(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_MAJORMINOR).la
libgstxvimagesink_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = xvimagesink.h 
