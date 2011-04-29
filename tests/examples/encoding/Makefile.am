examples = encoding

encoding_SOURCES = gstcapslist.c encoding.c
EXTRA_DIST = gstcapslist.h

noinst_PROGRAMS = $(examples)

LDADD = $(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
AM_CFLAGS = -I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
