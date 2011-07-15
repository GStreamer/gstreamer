plugin_LTLIBRARIES = libgstvaapisink.la

libgstvaapi_CFLAGS = \
	$(LIBVA_CFLAGS)		\
	-I$(top_srcdir)/gst-libs

if USE_VAAPISINK_GLX
libgstvaapi_LIBS = \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-glx-$(GST_MAJORMINOR).la
else
libgstvaapi_LIBS = \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-x11-$(GST_MAJORMINOR).la
endif

libgstvaapisink_la_SOURCES =	\
	gstvaapisink.c		\
	$(NULL)

noinst_HEADERS =		\
	gstvaapisink.h		\
	$(NULL)

libgstvaapisink_la_CFLAGS =	\
	$(libgstvaapi_CFLAGS)	\
	$(GST_CFLAGS)		\
	$(GST_VIDEO_CFLAGS)	\
	$(GST_INTERFACES_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)

libgstvaapisink_la_LIBADD =	\
	$(libgstvaapi_LIBS)	\
	$(GST_LIBS)		\
	$(GST_VIDEO_LIBS)	\
	$(GST_INTERFACES_LIBS)	\
	$(GST_PLUGINS_BASE_LIBS)

libgstvaapisink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvaapisink_la_LIBTOOLFLAGS = --tag=disable-static

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
