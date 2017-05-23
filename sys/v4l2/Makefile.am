plugin_LTLIBRARIES = libgstvideo4linux2.la

include $(top_srcdir)/common/gst-glib-gen.mak

libgstvideo4linux2_la_SOURCES = gstv4l2.c \
				gstv4l2allocator.c \
				gstv4l2colorbalance.c \
				gstv4l2deviceprovider.c \
				gstv4l2object.c \
				gstv4l2bufferpool.c \
				gstv4l2sink.c \
				gstv4l2src.c \
				gstv4l2radio.c \
				gstv4l2tuner.c \
				gstv4l2transform.c \
				gstv4l2videodec.c \
				gstv4l2videoenc.c \
				gstv4l2h264enc.c \
				gstv4l2vidorient.c \
				v4l2_calls.c \
				v4l2-utils.c \
				tuner.c \
				tunerchannel.c \
				tunernorm.c

libgstvideo4linux2_la_CFLAGS =   $(GST_PLUGINS_BASE_CFLAGS) \
				 $(GST_BASE_CFLAGS) \
				 $(GST_CFLAGS) \
				 $(X_CFLAGS) \
				 $(LIBV4L2_CFLAGS) \
				 $(GUDEV_CFLAGS)

libgstvideo4linux2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideo4linux2_la_LIBADD =   $(GST_PLUGINS_BASE_LIBS) \
				 -lgstallocators-$(GST_API_VERSION) \
				 -lgstvideo-$(GST_API_VERSION) \
				 $(GST_BASE_LIBS) \
				 $(GST_LIBS) \
				 $(LIBV4L2_LIBS) \
				 $(GUDEV_LIBS) \
				 $(LIBRT)

noinst_HEADERS = \
	ext/types-compat.h \
	ext/v4l2-common.h \
	ext/v4l2-controls.h \
	ext/videodev2.h \
	gstv4l2allocator.h \
	gstv4l2bufferpool.h \
	gstv4l2colorbalance.h \
	gstv4l2deviceprovider.h \
	gstv4l2object.h \
	gstv4l2sink.h \
	gstv4l2src.h \
	gstv4l2radio.h \
	gstv4l2tuner.h \
	gstv4l2transform.h \
	gstv4l2videodec.h \
	gstv4l2videoenc.h \
	gstv4l2h264enc.h \
	gstv4l2vidorient.h \
	v4l2_calls.h \
	v4l2-utils.h \
	tuner.h \
	tunerchannel.h \
	tunernorm.h

