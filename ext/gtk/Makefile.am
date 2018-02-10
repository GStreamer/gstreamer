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
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_GL_CFLAGS) \
	$(GST_CFLAGS) \
	$(GTK3_CFLAGS)
libgstgtk_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GTK3_LIBS)

libgstgtk_la_SOURCES = $(sources)
libgstgtk_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

if USE_GTK3_GL
libgstgtk_la_SOURCES += gstgtkglsink.c gstgtkglsink.h gtkgstglwidget.c gtkgstglwidget.h
libgstgtk_la_LIBADD += $(GST_GL_LIBS)
endif

plugin_LTLIBRARIES = libgstgtk.la
