libgstinterfacesincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/colorbalance

libgstinterfacesinclude_HEADERS = \
	colorbalance.h \
	colorbalancechannel.h

lib_LTLIBRARIES = libgstcolorbalance.la

libgstcolorbalance_la_SOURCES = \
	colorbalance.c \
	colorbalancechannel.c
libgstcolorbalance_la_LIBADD = 
libgstcolorbalance_la_CFLAGS = $(GST_CFLAGS) $(GST_OPT_CFLAGS)
libgstcolorbalance_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
