plugin_LTLIBRARIES = libgstvideobox.la

ORC_SOURCE=gstvideoboxorc
include $(top_srcdir)/common/orc.mak

libgstvideobox_la_SOURCES = gstvideobox.c
nodist_libgstvideobox_la_SOURCES = $(ORC_NODIST_SOURCES)
libgstvideobox_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
			   $(GST_CONTROLLER_CFLAGS) \
			   $(GST_BASE_CFLAGS) \
			   $(GST_CFLAGS) \
			   $(ORC_CFLAGS)
libgstvideobox_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
			   $(GST_CONTROLLER_LIBS) \
			   $(GST_BASE_LIBS) \
			   $(GST_LIBS) \
			   $(ORC_LIBS) \
			   -lgstvideo-@GST_MAJORMINOR@
libgstvideobox_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideobox_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstvideobox.h
EXTRA_DIST += README
