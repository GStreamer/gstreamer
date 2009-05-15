
plugin_LTLIBRARIES = libgstschro.la

noinst_HEADERS = \
	gstschroutils.h

libgstschro_la_SOURCES = \
	gstschro.c \
	gstschrodec.c \
	gstschroenc.c \
	gstschroparse.c \
	gstschroutils.c
libgstschro_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(SCHRO_CFLAGS)
libgstschro_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_MAJORMINOR).la \
	$(GST_LIBS) \
	$(SCHRO_LIBS)
libgstschro_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstschro_la_LIBTOOLFLAGS = --tag=disable-static


