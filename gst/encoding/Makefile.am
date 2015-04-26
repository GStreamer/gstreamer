plugin_LTLIBRARIES = libgstencodebin.la

libgstencodebin_la_SOURCES = 	\
	gstencodebin.c		\
	gstsmartencoder.c	\
	gststreamcombiner.c	\
	gststreamsplitter.c

libgstencodebin_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstencodebin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstencodebin_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_API_VERSION@.la \
	$(GST_LIBS)
libgstencodebin_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = 		\
	gstencodebin.h		\
	gststreamcombiner.h	\
	gststreamsplitter.h	\
	gstsmartencoder.h	\
	gststreamcombinerpad.h

include $(top_srcdir)/common/gst-glib-gen.mak
