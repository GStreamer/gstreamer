glib_gen_prefix = __gst_camerabin
glib_gen_basename = gstcamerabin

include $(top_srcdir)/common/gst-glib-gen.mak

built_sources = gstcamerabin-marshal.c
built_headers = gstcamerabin-marshal.h

BUILT_SOURCES = $(built_sources) $(built_headers)

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = gstcamerabin-marshal.list

plugin_LTLIBRARIES = libgstcamerabin.la

libgstcamerabin_la_SOURCES = gstcamerabin.c             \
			  gstcamerabincolorbalance.c \
			  camerabinimage.c           \
			  camerabinvideo.c           \
			  camerabingeneral.c         \
			  camerabinpreview.c         \
			  gstcamerabinphotography.c

nodist_libgstcamerabin_la_SOURCES = $(built_sources)
libgstcamerabin_la_CFLAGS = \
	-I$(top_builddir)/gst-libs \
	$(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API
libgstcamerabin_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstphotography-$(GST_MAJORMINOR).la \
	$(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstinterfaces-$(GST_MAJORMINOR)

libgstcamerabin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcamerabin_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstcamerabin.h             \
		 gstcamerabincolorbalance.h \
		 camerabinimage.h           \
		 camerabinvideo.h           \
		 camerabingeneral.h         \
		 camerabinpreview.h         \
		 gstcamerabinphotography.h 
