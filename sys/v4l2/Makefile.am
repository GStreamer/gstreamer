plugin_LTLIBRARIES = libgstvideo4linux2.la

if USE_XVIDEO
xv_source = gstv4l2videooverlay.c
xv_libs = $(X_LIBS) $(XVIDEO_LIBS)
else
xv_source =
xv_libs =
endif

include $(top_srcdir)/common/gst-glib-gen.mak

libgstvideo4linux2_la_SOURCES = gstv4l2.c \
				gstv4l2colorbalance.c \
				gstv4l2object.c \
				gstv4l2bufferpool.c \
				gstv4l2sink.c \
				gstv4l2src.c \
				gstv4l2radio.c \
				gstv4l2tuner.c \
				gstv4l2vidorient.c \
				v4l2_calls.c \
				tuner.c \
				tunerchannel.c \
				tunernorm.c \
				$(xv_source)

libgstvideo4linux2_la_CFLAGS =   $(GST_PLUGINS_BASE_CFLAGS) \
				 $(GST_BASE_CFLAGS) \
				 $(GST_CFLAGS) \
				 $(X_CFLAGS) \
				 $(LIBV4L2_CFLAGS) \
				 $(GUDEV_CFLAGS)

libgstvideo4linux2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideo4linux2_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
libgstvideo4linux2_la_LIBADD =   $(GST_PLUGINS_BASE_LIBS) \
				 -lgstallocators-$(GST_API_VERSION) \
				 -lgstvideo-$(GST_API_VERSION) \
				 $(GST_BASE_LIBS) \
				 $(GST_LIBS) \
				 $(xv_libs) \
				 $(LIBV4L2_LIBS) \
				 $(GUDEV_LIBS) \
				 $(LIBRT)

noinst_HEADERS = \
	gstv4l2bufferpool.h \
	gstv4l2colorbalance.h \
	gstv4l2object.h \
	gstv4l2sink.h \
	gstv4l2src.h \
	gstv4l2radio.h \
	gstv4l2tuner.h \
	gstv4l2vidorient.h \
	gstv4l2videooverlay.h \
	v4l2_calls.h \
	tuner.h \
	tunerchannel.h \
	tunernorm.h

