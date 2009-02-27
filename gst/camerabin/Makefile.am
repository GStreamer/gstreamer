glib_enum_prefix = __gst_camerabin

include $(top_srcdir)/common/glib-gen.mak

built_sources = gstcamerabin-marshal.c
built_headers = gstcamerabin-marshal.h

BUILT_SOURCES = $(built_sources) $(built_headers)

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = gstcamerabin-marshal.list

plugin_LTLIBRARIES = libgstcamerabin.la

libgstcamerabin_la_SOURCES = gstcamerabin.c             \
			  gstcamerabinxoverlay.c     \
			  gstcamerabincolorbalance.c \
			  camerabinimage.c           \
			  camerabinvideo.c           \
			  camerabingeneral.c         \
			  gstcamerabinphotography.c

nodist_libgstcamerabin_la_SOURCES = $(built_sources)

# libcamerabin_ladir = $(includedir)/gstreamer-@GST_MAJORMINOR@/camerabin

# libcamerabin_la_HEADERS = gstcamerabin.h

libgstcamerabin_la_CFLAGS = \
	$(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)

libgstcamerabin_la_LIBADD = \
	$(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstinterfaces-$(GST_MAJORMINOR)    \
	$(top_builddir)/gst-libs/gst/interfaces/libgstphotography-$(GST_MAJORMINOR).la

libgstcamerabin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcamerabin__la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstcamerabin.h             \
		 gstcamerabinxoverlay.h     \
		 gstcamerabincolorbalance.h \
		 camerabinimage.h           \
		 camerabinvideo.h           \
		 camerabingeneral.h         \
		 gstcamerabinphotography.h  \
		 $(built_headers)
