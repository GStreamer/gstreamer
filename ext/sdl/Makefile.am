plugin_LTLIBRARIES = libgstsdl.la 

libgstsdl_la_SOURCES = \
	gstsdl.c \
	sdlvideosink.c \
	sdlaudiosink.c

libgstsdl_la_CFLAGS =  $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(SDL_CFLAGS)
libgstsdl_la_LIBADD =  $(GST_PLUGINS_BASE_LIBS) \
				-lgstvideo-$(GST_MAJORMINOR) \
				-lgstaudio-$(GST_MAJORMINOR) \
				-lgstinterfaces-$(GST_MAJORMINOR) \
				$(SDL_LIBS)
libgstsdl_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 
libgstsdl_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	sdlvideosink.h \
	sdlaudiosink.h
