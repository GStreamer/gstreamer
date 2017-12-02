plugin_LTLIBRARIES = libgstcompositor.la

ORC_SOURCE=compositororc

include $(top_srcdir)/common/orc.mak

libgstcompositor_la_SOURCES = \
	blend.c \
	compositor.c


nodist_libgstcompositor_la_SOURCES = $(ORC_NODIST_SOURCES)
libgstcompositor_la_CFLAGS =  \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(ORC_CFLAGS)
libgstcompositor_la_LIBADD =  \
	$(top_builddir)/gst-libs/gst/video/libgstbadvideo-$(GST_API_VERSION).la \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(ORC_LIBS) $(LIBM)
libgstcompositor_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS = \
	blend.h \
	compositor.h \
	compositorpad.h
