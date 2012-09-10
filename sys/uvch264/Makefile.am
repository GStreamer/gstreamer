glib_gen_prefix = __gst_uvc_h264
glib_gen_basename = gstuvch264

include $(top_srcdir)/common/gst-glib-gen.mak

built_sources = gstuvch264-marshal.c
built_headers = gstuvch264-marshal.h

BUILT_SOURCES = $(built_sources) $(built_headers)

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = gstuvch264-marshal.list


plugin_LTLIBRARIES = libgstuvch264.la

libgstuvch264_la_SOURCES = gstuvch264.c \
			   gstuvch264_mjpgdemux.c \
			   gstuvch264_src.c \
			   uvc_h264.c

nodist_libgstuvch264_la_SOURCES = $(built_sources)

libgstuvch264_la_CFLAGS =   $(GST_PLUGINS_BAD_CFLAGS) \
			    $(GST_PLUGINS_BASE_CFLAGS) \
	  		    $(GST_BASE_CFLAGS) \
			    $(GST_VIDEO_CFLAGS) \
			    $(GST_CFLAGS) \
			    $(G_UDEV_CFLAGS) \
			    $(LIBUSB_CFLAGS) \
			    -DGST_USE_UNSTABLE_API

libgstuvch264_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstuvch264_la_LIBTOOLFLAGS = --tag=disable-static

libgstuvch264_la_LIBADD =   $(GST_PLUGINS_BASE_LIBS) \
			    $(GST_BASE_LIBS) \
			    $(GST_PLUGINS_BASE_LIBS) \
			    $(GST_VIDEO_LIBS) \
			    $(GST_LIBS) \
			    $(G_UDEV_LIBS) \
			    $(LIBUSB_LIBS) \
			    $(top_builddir)/gst-libs/gst/basecamerabinsrc/libgstbasecamerabinsrc-$(GST_MAJORMINOR).la

noinst_HEADERS = gstuvch264_mjpgdemux.h \
		 gstuvch264_src.h \
		 uvc_h264.h
