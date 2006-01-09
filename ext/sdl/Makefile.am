plugin_LTLIBRARIES = libgstsdlvideosink.la 

libgstsdlvideosink_la_SOURCES = gstsdl.c \
				sdlvideosink.c \
				sdlaudiosink.c

libgstsdlvideosink_la_CFLAGS =  $(GST_PLUGINS_BASE_CFLAGS) \
				$(SDL_CFLAGS)

libgstsdlvideosink_la_LIBADD =  $(GST_PLUGINS_BASE_LIBS) \
				-lgstvideo-$(GST_MAJORMINOR) \
				-lgstaudio-@GST_MAJORMINOR@ \
				-lgstinterfaces-$(GST_MAJORMINOR) \
				$(SDL_LIBS)

libgstsdlvideosink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 

noinst_HEADERS = 	sdlvideosink.h \
			sdlaudiosink.h
