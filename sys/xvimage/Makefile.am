
plugin_LTLIBRARIES = libgstxvimagesink.la

libgstxvimagesink_la_SOURCES =  xvimagesink.c 
libgstxvimagesink_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS)
libgstxvimagesink_la_LIBADD = $(X_LIBS) $(XVIDEO_LIBS) $(XSHM_LIBS) \
	$(top_builddir)/gst-libs/gst/libgstinterfaces-$(GST_MAJORMINOR).la
libgstxvimagesink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = xvimagesink.h 

