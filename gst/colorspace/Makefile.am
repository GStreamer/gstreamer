plugin_LTLIBRARIES = libgstcolorspace.la

ORC_SOURCE=gstcolorspaceorc
include $(top_srcdir)/common/orc.mak

libgstcolorspace_la_SOURCES = gstcolorspace.c
nodist_libgstcolorspace_la_SOURCES = $(ORC_NODIST_SOURCES)
libgstcolorspace_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(ORC_CFLAGS)
libgstcolorspace_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(ORC_LIBS)
libgstcolorspace_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcolorspace_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstcolorspace.h


