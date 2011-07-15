plugin_LTLIBRARIES = libgstvaapiconvert.la

libgstvaapi_CFLAGS = \
	$(LIBVA_CFLAGS)		\
	-I$(top_srcdir)/gst-libs

libgstvaapi_LIBS = \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-$(GST_MAJORMINOR).la

libgstvaapiconvert_la_SOURCES =	\
	gstvaapiconvert.c	\
	$(NULL)

noinst_HEADERS =		\
	gstvaapiconvert.h	\
	$(NULL)

libgstvaapiconvert_la_CFLAGS =	\
	$(libgstvaapi_CFLAGS)	\
	$(GST_CFLAGS)		\
	$(GST_BASE_CFLAGS)	\
	$(GST_VIDEO_CFLAGS)	\
	$(GST_PLUGINS_BASE_CFLAGS)

libgstvaapiconvert_la_LIBADD =	\
	$(libgstvaapi_LIBS)	\
	$(GST_LIBS)		\
	$(GST_BASE_LIBS)	\
	$(GST_VIDEO_LIBS)	\
	$(GST_PLUGINS_BASE_LIBS)

libgstvaapiconvert_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvaapiconvert_la_LIBTOOLFLAGS = --tag=disable-static

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
