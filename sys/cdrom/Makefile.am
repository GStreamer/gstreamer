plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstcdplayer.la

libgstcdplayer_la_SOURCES = gstcdplayer.c gstcdplayer_ioctl.c
libgstcdplayer_la_CFLAGS = $(GST_CFLAGS)
libgstcdplayer_la_LIBADD =
libgstcdplayer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstcdplayer.h gstcdplayer_ioctl.h gstcdplayer_ioctl_solaris.h gstcdplayer_ioctl_bsd.h
