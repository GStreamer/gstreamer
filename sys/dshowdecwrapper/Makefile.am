plugin_LTLIBRARIES = libgstdshowdecwrapper.la

libgstdshowdecwrapper_la_SOURCES = gstdshowaudiodec.cpp gstdshowdecwrapper.cpp \
								   gstdshowvideodec.cpp gstdshowutil.cpp \
								   gstdshowfakesrc.cpp

libgstdshowdecwrapper_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/gst-libs/gst -DLIBGSTDSHOWDECWRAPPER_EXPORTS
libgstdshowdecwrapper_la_LIBADD = $(top_builddir)/gst-libs/gst/dshow/libgstdshow-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstdshowdecwrapper_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstdshowaudiodec.h gstdshowvideodec.h gstdshowfakesrc.h \
				 gstdshowutil.h

