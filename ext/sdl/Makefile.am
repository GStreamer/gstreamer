plugin_LTLIBRARIES = libgstsdl.la 

libgstsdl_la_SOURCES = \
	gstsdl.c \
	sdlvideosink.c \
	sdlaudiosink.c

libgstsdl_la_CFLAGS =  $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(SDL_CFLAGS)
libgstsdl_la_LIBADD =  $(GST_PLUGINS_BASE_LIBS) \
				-lgstvideo-$(GST_API_VERSION) \
				-lgstaudio-$(GST_API_VERSION) \
				-lgstinterfaces-$(GST_API_VERSION) \
				$(SDL_LIBS)
libgstsdl_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 
libgstsdl_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	sdlvideosink.h \
	sdlaudiosink.h
