
plugin_LTLIBRARIES = libgstsdlvideosink.la 

libgstsdlvideosink_la_SOURCES =  sdlvideosink.c
libgstsdlvideosink_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS) $(SDL_CFLAGS)
libgstsdlvideosink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 
libgstsdlvideosink_la_LIBADD = $(SDL_LIBS) \
        $(top_builddir)/../gst-plugins-base/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la \
        $(top_builddir)/../gst-plugins-base/gst-libs/gst/video/libgstvideo-$(GST_MAJORMINOR).la \
        $(X_LIBS) $(XVIDEO_LIBS) $(XSHM_LIBS)
libgstsdlvideosink_la_DEPENDENCIES = $(top_builddir)/../gst-plugins-base/gst-libs/gst/video/libgstvideo-$(GST_MAJORMINOR).la

noinst_HEADERS = sdlvideosink.h


