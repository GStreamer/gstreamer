
plugin_LTLIBRARIES = libgstpuzzle.la

libgstpuzzle_la_SOURCES = \
			gstvideoimage.c \
			gstpuzzle.c
			
libgstpuzzle_la_CFLAGS = $(GST_CFLAGS) $(LIBOIL_CFLAGS) -I$(top_srcdir)/gst/videofilter
libgstpuzzle_la_LIBADD =
libgstpuzzle_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(LIBOIL_LIBS)
libgstpuzzle_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstvideoimage.h
