plugin_LTLIBRARIES = libgstximage.la

libgstximage_la_SOURCES =  ximagesink.c ximage.c
libgstximage_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS)
libgstximage_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstximage_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_MAJORMINOR).la \
$(X_LIBS) $(XSHM_LIBS)
libgstximage_la_DEPENDENCIES = $(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_MAJORMINOR).la

noinst_HEADERS = ximagesink.h 
