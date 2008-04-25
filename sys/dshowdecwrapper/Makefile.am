plugin_LTLIBRARIES = libgstdshowdecwrapper.la

libgstdshowdecwrapper_la_SOURCES = gstdshowaudiodec.c gstdshowdecwrapper.c gstdshowvideodec.c

libgstdshowdecwrapper_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/gst-libs/gst -DLIBGSTDSHOWDECWRAPPER_EXPORTS
libgstdshowdecwrapper_la_LIBADD = $(GST_LIBS) $(top_builddir)/gst-libs/gst/dshow/libgstdshow-@GST_MAJORMINOR@.la
libgstdshowdecwrapper_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstdshowaudiodec.h gstdshowdecwrapper.h gstdshowvideodec.h
