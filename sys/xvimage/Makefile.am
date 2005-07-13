plugin_LTLIBRARIES = libgstxvimagesink.la

libgstxvimagesink_la_SOURCES =  xvimagesink.c 
libgstxvimagesink_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS)
libgstxvimagesink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstxvimagesink_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_MAJORMINOR).la \
	$(X_LIBS) $(XVIDEO_LIBS) $(XSHM_LIBS)
libgstxvimagesink_la_DEPENDENCIES = $(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_MAJORMINOR).la

noinst_HEADERS = xvimagesink.h 
