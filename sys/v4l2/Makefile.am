plugin_LTLIBRARIES = \
	libgstvideo4linux2.la

if USE_XVIDEO
xv_source = gstv4l2xoverlay.c
xv_libs = $(X_LIBS) $(XVIDEO_LIBS)
else
xv_source =
xv_libs =
endif

libgstvideo4linux2_la_SOURCES = \
	gstv4l2element.c v4l2_calls.c \
	gstv4l2src.c v4l2src_calls.c \
	gstv4l2.c \
	gstv4l2tuner.c \
	$(xv_source) \
	gstv4l2colorbalance.c
libgstvideo4linux2_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS)
libgstvideo4linux2_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/libgstinterfaces-@GST_MAJORMINOR@.la
libgstvideo4linux2_la_LDFLAGS = \
	$(GST_PLUGIN_LDFLAGS) \
	$(xv_libs)

noinst_HEADERS = gstv4l2element.h v4l2_calls.h \
		gstv4l2src.h v4l2src_calls.h \
		gstv4l2tuner.h gstv4l2xoverlay.h \
		gstv4l2colorbalance.h
