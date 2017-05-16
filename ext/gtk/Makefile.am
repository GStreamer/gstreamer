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

libgstgtk_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GTK3_CFLAGS) \
	$(GST_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstgtk_la_LIBADD = \
	$(GTK3_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION)

libgstgtk_la_SOURCES = $(sources)
libgstgtk_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

if USE_GTK3_GL
if USE_GL
libgstgtk_la_SOURCES += gstgtkglsink.c gstgtkglsink.h gtkgstglwidget.c gtkgstglwidget.h
libgstgtk_la_LIBADD += \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-$(GST_API_VERSION).la
endif
endif

plugin_LTLIBRARIES = libgstgtk.la
