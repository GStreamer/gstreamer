plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstarts.la

SUFFIXES = .idl
.idl.cc:
	mcopidl -t $(ARTS_CXXFLAGS) $<

# mcopidl Extension Expansion Technology clean up
CLEANFILES = gst_artsio.h gst_artsio.cc gst_artsio.mcopclass gst_artsio.mcoptype

libgstarts_la_SOURCES = gst_arts.c gst_artsio.idl gst_artsio_impl.cc
libgstarts_la_CFLAGS = $(GST_CFLAGS)
libgstarts_la_CXXFLAGS = $(ARTS_CXXFLAGS) $(GST_CFLAGS)
libgstarts_la_LIBADD = $(ARTS_LIBS) -lartsflow -lartsflow_idl
libgstarts_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gst_arts.h gst_artsio_impl.h

dist-hook:
	rm -f $(distdir)/gst_artsio.cc
