plugin_LTLIBRARIES = libgstvideo4linux.la

if USE_XVIDEO
xv_source = gstv4lxoverlay.c
xv_libs = $(X_LIBS) $(XVIDEO_LIBS)
else
xv_source =
xv_libs =
endif

libgstvideo4linux_la_SOURCES = \
	gstv4l.c \
	gstv4lcolorbalance.c \
	gstv4lelement.c \
	gstv4lsrc.c \
	gstv4ltuner.c \
	v4l_calls.c \
	v4lsrc_calls.c $(xv_source)

#	gstv4ljpegsrc.c \
#	gstv4lmjpegsrc.c v4lmjpegsrc_calls.c \
#	gstv4lmjpegsink.c v4lmjpegsink_calls.c

libgstvideo4linux_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(X_CFLAGS) \
	$(GUDEV_CFLAGS)
libgstvideo4linux_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(xv_libs) \
	$(GUDEV_LIBS)
libgstvideo4linux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideo4linux_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstv4lelement.h v4l_calls.h \
		gstv4lsrc.h v4lsrc_calls.h \
		gstv4ljpegsrc.h \
		gstv4lmjpegsrc.h v4lmjpegsrc_calls.h \
		gstv4lmjpegsink.h v4lmjpegsink_calls.h \
		videodev_mjpeg.h \
		gstv4ltuner.h gstv4lxoverlay.h \
		gstv4lcolorbalance.h
