plugin_LTLIBRARIES = \
	libgstvideo4linux2.la

libgstvideo4linux2_la_SOURCES = \
	gstv4l2element.c v4l2_calls.c \
	v4l2-overlay_calls.c \
	gstv4l2src.c v4l2src_calls.c \
	gstv4l2.c \
	gstv4l2tuner.c \
	gstv4l2xoverlay.c \
	gstv4l2colorbalance.c
libgstvideo4linux2_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS)
libgstvideo4linux2_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/libgstinterfaces-@GST_MAJORMINOR@.la
libgstvideo4linux2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstv4l2element.h v4l2_calls.h \
		gstv4l2src.h v4l2src_calls.h \
		gstv4l2tuner.h gstv4l2xoverlay.h \
		gstv4l2colorbalance.h
