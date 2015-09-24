# preamble
NULL =
BUILT_SOURCES =
CLEANFILES =
EXTRA_DIST =
DISTCLEANFILES =
lib_LTLIBRARIES =

# source
sources = \
	gtkgstbasewidget.c \
	gtkgstbasewidget.h \
	gtkgstwidget.c \
	gtkgstwidget.h \
	gstgtkbasesink.c \
	gstgtkbasesink.h \
	gstgtksink.c \
	gstgtksink.h \
	gstgtkutils.c \
	gstgtkutils.h \
	gstplugin.c \
	$(NULL)

libgstgtksink_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GTK3_CFLAGS) \
	$(GST_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstgtksink_la_LIBADD = \
	$(GTK3_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION)

libgstgtksink_la_SOURCES = $(sources)
libgstgtksink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgtksink_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

if USE_GTK3_GL
if USE_GL
libgstgtksink_la_SOURCES += gstgtkglsink.c gstgtkglsink.h gtkgstglwidget.c gtkgstglwidget.h
libgstgtksink_la_LIBADD += \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-$(GST_API_VERSION).la
endif
endif

plugin_LTLIBRARIES = libgstgtksink.la
