
plugin_LTLIBRARIES = libgstqcam.la

EXTRA_DIST = qcam-os.c qcam-Linux.c

libgstqcam_la_SOURCES =	gstqcamsrc.c qcam-lib.c exposure.c
libgstqcam_la_CFLAGS = -O2 $(GST_CFLAGS)
libgstqcam_la_LIBADD =
libgstqcam_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstqcam_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstqcamsrc.h qcam-os.h qcam.h qcamip.h qcam-Linux.h
