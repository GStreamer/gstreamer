
plugin_LTLIBRARIES = libgstglimagesink.la

libgstglimagesink_la_SOURCES =  glimagesink.c 
libgstglimagesink_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS) $(GST_BASE_CFLAGS)
libgstglimagesink_la_LIBADD = $(X_LIBS) $(XSHM_LIBS) -lGL -lGLU \
	$(GST_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) -lgstinterfaces-$(GST_MAJORMINOR)
libgstglimagesink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = glimagesink.h 

