plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstqcam.la

EXTRA_DIST = qcam-os.c qcam-Linux.c

libgstqcam_la_SOURCES =	gstqcamsrc.c qcam-lib.c exposure.c
libgstqcam_la_CFLAGS = -O2 $(GST_CFLAGS)

noinst_HEADERS = gstqcamsrc.h qcam-os.h qcam.h qcamip.h qcam-Linux.h
