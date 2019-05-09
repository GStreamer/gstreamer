plugin_LTLIBRARIES = libgstmpegtsmux.la

SUBDIRS = tsmux

libgstmpegtsmux_la_SOURCES = \
  gstatscmux.c \
	gstmpegtsmux.c \
	gstbasetsmux.c \
	gstbasetsmuxaac.c \
	gstbasetsmuxttxt.c \
	gstbasetsmuxopus.c \
	gstbasetsmuxjpeg2000.c \
	gstmpegtsmuxplugin.c

libgstmpegtsmux_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
			    -I$(top_srcdir) \
			    $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmpegtsmux_la_LIBADD = $(top_builddir)/gst/mpegtsmux/tsmux/libtsmux.la \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	-lgstaudio-@GST_API_VERSION@ -lgsttag-@GST_API_VERSION@ \
	-lgstpbutils-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstmpegtsmux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
  gstatscmux.h \
	gstmpegtsmux.h \
	gstbasetsmux.h \
	gstbasetsmuxaac.h \
	gstbasetsmuxttxt.h \
	gstbasetsmuxopus.h \
	gstbasetsmuxjpeg2000.h
