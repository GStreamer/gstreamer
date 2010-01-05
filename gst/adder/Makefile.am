plugin_LTLIBRARIES = libgstadder.la

libgstadder_la_SOURCES = gstadder.c
libgstadder_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
#$(LIBOIL_CFLAGS)
libgstadder_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstadder_la_LIBADD =  \
	         $(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la \
		 $(GST_BASE_LIBS) $(GST_LIBS)
#$(LIBOIL_LIBS)
libgstadder_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstadder.h
