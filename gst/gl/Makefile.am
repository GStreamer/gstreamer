plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstglsink.la

libgstglsink_la_SOURCES = gstglsink.c gstglxwindow.c gstgl_rgbimage.c gstgl_nvimage.c
libgstglsink_la_CFLAGS = $(GST_CFLAGS)
libgstglsink_la_LIBADD = $(X_LIBS) $(XVIDEO_LIBS) -lGL -lGLU -lglut
libgstglsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstglsink.h
