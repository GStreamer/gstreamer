plugin_LTLIBRARIES = libgstvaapidecode.la

libgstvaapi_CFLAGS = \
	$(LIBVA_CFLAGS)		\
	-I$(top_srcdir)/gst-libs

libgstvaapi_LIBS = \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-x11-$(GST_MAJORMINOR).la

libgstvaapidecode_la_SOURCES =	\
	gstvaapidecode.c	\
	$(NULL)

noinst_HEADERS =		\
	gstvaapidecode.h	\
	$(NULL)

libgstvaapidecode_la_CFLAGS =	\
	$(libgstvaapi_CFLAGS)	\
	$(GST_CFLAGS)		\
	$(GST_BASE_CFLAGS)	\
	$(GST_VIDEO_CFLAGS)	\
	$(GST_PLUGINS_BASE_CFLAGS)

libgstvaapidecode_la_LIBADD =	\
	$(libgstvaapi_LIBS)	\
	$(GST_LIBS)		\
	$(GST_BASE_LIBS)	\
	$(GST_VIDEO_LIBS)	\
	$(GST_PLUGINS_BASE_LIBS)

libgstvaapidecode_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvaapidecode_la_LIBTOOLFLAGS = --tag=disable-static

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
