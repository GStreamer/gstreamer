plugin_LTLIBRARIES = libgstvideo4linux.la

if USE_XVIDEO
xv_source = gstv4lxoverlay.c
xv_libs = $(X_LIBS) $(XVIDEO_LIBS)
else
xv_source =
xv_libs =
endif

libgstvideo4linux_la_SOURCES = \
	gstv4lelement.c v4l_calls.c \
	gstv4lsrc.c v4lsrc_calls.c \
	gstv4lmjpegsrc.c v4lmjpegsrc_calls.c \
	gstv4lmjpegsink.c v4lmjpegsink_calls.c \
	gstv4l.c \
	gstv4ltuner.c \
	$(xv_source) \
	gstv4lcolorbalance.c
libgstvideo4linux_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS)
libgstvideo4linux_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/libgstinterfaces-@GST_MAJORMINOR@.la
libgstvideo4linux_la_LDFLAGS = \
	$(GST_PLUGIN_LDFLAGS) \
	$(xv_libs)

noinst_HEADERS = gstv4lelement.h v4l_calls.h \
		gstv4lsrc.h v4lsrc_calls.h \
		gstv4lmjpegsrc.h v4lmjpegsrc_calls.h \
		gstv4lmjpegsink.h v4lmjpegsink_calls.h \
		videodev_mjpeg.h \
		gstv4ltuner.h gstv4lxoverlay.h \
		gstv4lcolorbalance.h
