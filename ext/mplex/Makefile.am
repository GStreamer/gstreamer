plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstmplex.la

libgstmplex_la_SOURCES = gstmplex.cc
libgstmplex_la_CXXFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/gst-libs/ext/mplex/
libgstmplex_la_LIBADD = $(LIBM_LIBS) $(top_builddir)/gst-libs/ext/mplex/libmplex.la
libgstmplex_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmplex.h


