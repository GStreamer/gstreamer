plugin_LTLIBRARIES = libgstencoding.la

libgstencoding_la_SOURCES = 	\
	gstencodebin.c		\
	gstsmartencoder.c	\
	gststreamcombiner.c	\
	gststreamsplitter.c

libgstencoding_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstencoding_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstencoding_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_API_VERSION@.la \
	$(GST_LIBS)
libgstencoding_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = 		\
	gstencodebin.h		\
	gststreamcombiner.h	\
	gststreamsplitter.h	\
	gstsmartencoder.h	\
	gststreamcombinerpad.h

include $(top_srcdir)/common/gst-glib-gen.mak
