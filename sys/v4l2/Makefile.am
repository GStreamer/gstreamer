plugindir = $(libdir)/gst

plugin_LTLIBRARIES = \
	libgstv4l2element.la \
	libgstv4l2src.la

libgstv4l2element_la_SOURCES = gstv4l2element.c v4l2_calls.c v4l2-overlay_calls.c
libgstv4l2element_la_CFLAGS = $(GST_CFLAGS)
libgstv4l2element_la_LIBADD =
libgstv4l2element_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstv4l2src_la_SOURCES = gstv4l2src.c v4l2src_calls.c
libgstv4l2src_la_CFLAGS = $(GST_CFLAGS)
libgstv4l2src_la_LIBADD = libgstv4l2element.la
libgstv4l2src_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstv4l2element.h v4l2_calls.h \
		gstv4l2src.h v4l2src_calls.h
