
plugin_LTLIBRARIES = libgstcacasink.la

libgstcacasink_la_SOURCES =  gstcacasink.c
libgstcacasink_la_CFLAGS =  $(GST_CFLAGS) $(LIBCACA_CFLAGS)
libgstcacasink_la_LIBADD = $(LIBCACA_LIBS) \
	$(top_builddir)/gst-libs/gst/libgstinterfaces-$(GST_MAJORMINOR).la
libgstcacasink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstcacasink.h
