plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstac3parse.la

libgstac3parse_la_SOURCES = gstac3parse.c

noinst_HEADERS = gstac3parse.h

libgstac3parse_la_CFLAGS = -O3 $(FOMIT_FRAME_POINTER) $(GST_OPT_FLAGS) $(GST_FUNROLL_CFLAGS) $(GST_CFLAGS)
libgstac3parse_la_LIBADD =
libgstac3parse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

EXTRA_DIST = README
