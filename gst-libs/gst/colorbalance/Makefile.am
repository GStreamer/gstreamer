# variables used for enum/marshal generation
glib_enum_headers=$(colorbalance_headers)
glib_enum_define=GST_COLOR_BALANCE
glib_root=colorbalance
glib_enum_prefix=gst_color_balance

libgstcolorbalanceincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/colorbalance

colorbalance_headers = \
	colorbalance.h \
	colorbalancechannel.h

built_headers =	\
	$(glib_root)-marshal.h \
	$(glib_root)-enumtypes.h

libgstcolorbalanceinclude_HEADERS = $(colorbalance_headers) $(built_headers)

noinst_LTLIBRARIES = libgstcolorbalance.la

libgstcolorbalance_la_SOURCES = \
	colorbalance.c \
	colorbalancechannel.c \
	$(glib_root)-marshal.c \
	$(glib_root)-enumtypes.c
libgstcolorbalance_la_CFLAGS = $(GST_CFLAGS) $(GST_OPT_CFLAGS)

BUILT_SOURCES = \
	$(glib_root)-marshal.c \
	$(glib_root)-enumtypes.c \
        $(built_headers)

EXTRA_DIST = $(glib_root)-marshal.list

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/glib-gen.mak
