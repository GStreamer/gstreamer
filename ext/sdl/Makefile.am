
plugin_LTLIBRARIES = libgstsdlvideosink.la

libgstsdlvideosink_la_SOURCES =  sdlvideosink.c
## FIXME I have my doubts about these opti flags
libgstsdlvideosink_la_CFLAGS = $(GST_CFLAGS) $(SDL_CFLAGS)
libgstsdlvideosink_la_LIBADD = $(SDL_LIBS) \
		$(top_builddir)/gst-libs/gst/libgstinterfaces-@GST_MAJORMINOR@.la
libgstsdlvideosink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = sdlvideosink.h
