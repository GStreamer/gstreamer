plugin_LTLIBRARIES = libgstvideo4linux2.la

if USE_XVIDEO
xv_source = gstv4l2videooverlay.c
xv_libs = $(X_LIBS) $(XVIDEO_LIBS)
else
xv_source =
xv_libs =
endif


# variables used for enum/marshal generation
glib_enum_headers = tuner.h tunernorm.h	tunerchannel.h
glib_enum_define = GST_INTERFACES
glib_gen_prefix = gst_interfaces
glib_gen_basename = tuner

built_sources = \
	tuner-marshal.c \
	tuner-enumtypes.c

built_headers = \
	tuner-marshal.h \
	tuner-enumtypes.h

EXTRA_DIST = tuner-marshal.list

CLEANFILES = $(built_sources) $(built_headers)

BUILT_SOURCES = $(built_sources) $(built_headers)

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
nodist_libgstvideo4linux2_la_SOURCES = $(built_sources) $(built_headers)

libgstvideo4linux2_la_CFLAGS =   $(GST_PLUGINS_BASE_CFLAGS) \
				 $(GST_BASE_CFLAGS) \
				 $(GST_CFLAGS) \
				 $(X_CFLAGS) \
				 $(LIBV4L2_CFLAGS) \
				 $(GUDEV_CFLAGS)

libgstvideo4linux2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideo4linux2_la_LIBTOOLFLAGS = --tag=disable-static

libgstvideo4linux2_la_LIBADD =   $(GST_PLUGINS_BASE_LIBS) \
				 $(GST_BASE_LIBS) \
				 $(GST_PLUGINS_BASE_LIBS) \
				 -lgstvideo-$(GST_API_VERSION) \
				 $(GST_LIBS) \
				 $(xv_libs) \
				 $(LIBV4L2_LIBS) \
				 $(GUDEV_LIBS)

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

