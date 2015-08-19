plugin_LTLIBRARIES = libgstuvch264.la

libgstuvch264_la_SOURCES = gstuvch264.c \
			   gstuvch264_mjpgdemux.c \
			   gstuvch264_src.c \
			   uvc_h264.c

libgstuvch264_la_CFLAGS =   $(GST_PLUGINS_BAD_CFLAGS) \
			    $(GST_PLUGINS_BASE_CFLAGS) \
	  		    $(GST_BASE_CFLAGS) \
			    $(GST_CFLAGS) \
			    $(G_UDEV_CFLAGS) \
			    $(LIBUSB_CFLAGS) \
			    -DGST_USE_UNSTABLE_API

libgstuvch264_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstuvch264_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

libgstuvch264_la_LIBADD =   $(GST_PLUGINS_BASE_LIBS) \
			    -lgstvideo-$(GST_API_VERSION) \
			    $(GST_BASE_LIBS) \
			    $(GST_LIBS) \
			    $(G_UDEV_LIBS) \
			    $(LIBUSB_LIBS) \
			    $(top_builddir)/gst-libs/gst/basecamerabinsrc/libgstbasecamerabinsrc-@GST_API_VERSION@.la

noinst_HEADERS = gstuvch264_mjpgdemux.h \
		 gstuvch264_src.h \
		 uvc_h264.h
