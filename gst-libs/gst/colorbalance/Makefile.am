libgstcolorbalanceincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/colorbalance

libgstcolorbalanceinclude_HEADERS = \
	colorbalance.h \
	colorbalancechannel.h

noinst_LTLIBRARIES = libgstcolorbalance.la

libgstcolorbalance_la_SOURCES = \
	colorbalance.c \
	colorbalancechannel.c \
	colorbalancemarshal.c
libgstcolorbalance_la_CFLAGS = $(GST_CFLAGS) $(GST_OPT_CFLAGS)

BUILT_SOURCES = \
	colorbalancemarshal.c \
	colorbalancemarshal.h
built_headers =	\
	colorbalancemarshal.h

EXTRA_DIST = colorbalancemarshal.list

colorbalancemarshal.h: colorbalancemarshal.list
	glib-genmarshal --header --prefix=gst_color_balance_marshal $^ > colorbalancemarshal.h.tmp
	mv colorbalancemarshal.h.tmp colorbalancemarshal.h

colorbalancemarshal.c: colorbalancemarshal.list
	echo "#include \"colorbalancemarshal.h\"" >> colorbalancemarshal.c.tmp
	glib-genmarshal --body --prefix=gst_color_balance_marshal $^ >> colorbalancemarshal.c.tmp
	mv colorbalancemarshal.c.tmp colorbalancemarshal.c
