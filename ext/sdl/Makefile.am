plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libsdlvideosink.la

libsdlvideosink_la_SOURCES =  sdlvideosink.c
## FIXME I have my doubts about these opti flags
libsdlvideosink_la_CFLAGS = $(GST_CFLAGS) $(SDL_CFLAGS) -ffast-math -finline-functions
libsdlvideosink_la_LIBADD = $(GST_LIBS) $(SDL_LIBS)

noinst_HEADERS = sdlvideosink.h
