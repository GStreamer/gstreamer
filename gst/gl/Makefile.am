
plugin_LTLIBRARIES = libgstglimagesink.la

libgstglimagesink_la_SOURCES =  glimagesink.c 
libgstglimagesink_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS)
libgstglimagesink_la_LIBADD = $(X_LIBS) $(XSHM_LIBS) -lGL -lGLU\
	$(top_builddir)/gst-libs/gst/libgstinterfaces-$(GST_MAJORMINOR).la
libgstglimagesink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = glimagesink.h 

