# variables used for enum/marshal generation
glib_enum_headers=$(colorbalance_headers)
glib_enum_define=GST_COLOR_BALANCE
glib_enum_prefix=gst_color_balance

libgstcolorbalanceincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/colorbalance

colorbalance_headers = \
	colorbalance.h \
	colorbalancechannel.h

built_sources =	\
	colorbalance-marshal.c \
	colorbalance-enumtypes.c

built_headers =	\
	colorbalance-marshal.h \
	colorbalance-enumtypes.h

libgstcolorbalanceinclude_HEADERS = \
	$(colorbalance_headers)

nodist_libgstcolorbalanceinclude_HEADERS = \
	colorbalance-enumtypes.h

noinst_LTLIBRARIES = libgstcolorbalance.la

libgstcolorbalance_la_SOURCES = \
	colorbalance.c \
	colorbalancechannel.c

nodist_libgstcolorbalance_la_SOURCES = \
        $(built_sources) \
	colorbalance-marshal.h

libgstcolorbalance_la_CFLAGS = $(GST_CFLAGS)

BUILT_SOURCES = \
        $(built_sources) \
        $(built_headers)

EXTRA_DIST = colorbalance-marshal.list

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/glib-gen.mak
