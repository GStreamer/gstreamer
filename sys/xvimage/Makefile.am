
plugin_LTLIBRARIES = libgstxvimagesink.la

libgstxvimagesink_la_SOURCES =  xvimagesink.c 
libgstxvimagesink_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS)
libgstxvimagesink_la_LIBADD = $(X_LIBS) $(XVIDEO_LIBS) $(XSHM_LIBS)
libgstxvimagesink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) \
		$(top_builddir)/gst-libs/gst/navigation/libgstnavigation.la \
		$(top_builddir)/gst-libs/gst/xoverlay/libgstxoverlay.la

noinst_HEADERS = xvimagesink.h 

