libgstcolorbalanceincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/colorbalance

libgstcolorbalanceinclude_HEADERS = \
	colorbalance.h \
	colorbalancechannel.h

noinst_LTLIBRARIES = libgstcolorbalance.la

libgstcolorbalance_la_SOURCES = \
	colorbalance.c \
	colorbalancechannel.c
libgstcolorbalance_la_CFLAGS = $(GST_CFLAGS) $(GST_OPT_CFLAGS)
