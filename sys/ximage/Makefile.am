
plugin_LTLIBRARIES = libgstximagesink.la

libgstximagesink_la_SOURCES =  ximagesink.c 
libgstximagesink_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS)
libgstximagesink_la_LIBADD = $(X_LIBS) $(XSHM_LIBS) \
	$(top_builddir)/gst-libs/gst/libgstinterfaces-$(GST_MAJORMINOR).la
libgstximagesink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = ximagesink.h 

