
plugin_LTLIBRARIES = libgstglimagesink.la

libgstglimagesink_la_SOURCES = \
	glimagesink.c \
	glvideo.c \
	gstopengl.c \
	glextensions.c \
	gstglbuffer.c \
	gstglupload.c \
	gstgldownload.c \
	gstglfilter.c
libgstglimagesink_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstglimagesink_la_LIBADD = $(X_LIBS) $(XSHM_LIBS) -lGL \
	$(GST_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-$(GST_MAJORMINOR)
libgstglimagesink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	glimagesink.h \
	glvideo.h \
	glextensions.h \
	gstglbuffer.h
