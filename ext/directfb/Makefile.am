plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstdirectfbvideosink.la

libgstdirectfbvideosink_la_SOURCES =  directfbvideosink.c
libgstdirectfbvideosink_la_CFLAGS =	$(GST_CFLAGS)		\
					$(DIRECTFB_CFLAGS)	\
					$(GST_OPT_CFLAGS)
libgstdirectfbvideosink_la_LIBADD = $(DIRECTFB_LIBS) \
        $(top_builddir)/gst-libs/gst/libgstinterfaces-$(GST_MAJORMINOR).la
libgstdirectfbvideosink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = directfbvideosink.h
