plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libsdlvideosink.la
libsdlvideosink_la_SOURCES =  sdlvideosink.c


## FIXME I have my doubts about these opti flags
libsdlvideosink_la_CFLAGS = -ffast-math -finline-functions $(GST_CFLAGS) $(SDL_CFLAGS)
libsdlvideosink_la_LIBADD = $(SDL_LIBS)

noinst_HEADERS = sdlvideosink.h
