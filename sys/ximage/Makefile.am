
plugin_LTLIBRARIES = libgstximagesink.la

libgstximagesink_la_SOURCES =  ximagesink.c 
libgstximagesink_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS)
libgstximagesink_la_LIBADD = $(X_LIBS)
libgstximagesink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) \
		$(top_builddir)/gst-libs/gst/navigation/libgstnavigation.la \
		$(top_builddir)/gst-libs/gst/xoverlay/libgstxoverlay.la

noinst_HEADERS = ximagesink.h 

