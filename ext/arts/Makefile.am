
plugin_LTLIBRARIES = libgstarts.la

SUFFIXES = .idl
# on FC2, artsc-config --cflags has -pthread, which mcopidl doesn't like
# If someone has time and energy, please check if -pthread should be allowed
# in compile flags at all.
.idl.cc:
	mcopidl -t `echo $(ARTS_CXXFLAGS) | sed 's/-pthread//g'` $<

# mcopidl Extension Expansion Technology clean up
CLEANFILES = gst_artsio.h gst_artsio.cc gst_artsio.mcopclass gst_artsio.mcoptype

EXTRA_DIST = gst_artsio.idl
BUILT_SOURCES = gst_artsio.cc

libgstarts_la_SOURCES = gst_arts.c gst_artsio_impl.cc
nodist_libgstarts_la_SOURCES = gst_artsio.cc
libgstarts_la_CFLAGS = $(GST_CFLAGS)
libgstarts_la_CXXFLAGS = $(ARTS_CXXFLAGS) $(GST_CFLAGS)
libgstarts_la_LIBADD = $(ARTS_LIBS) -lartsflow -lartsflow_idl
libgstarts_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gst_arts.h gst_artsio_impl.h

dist-hook:
	rm -f $(distdir)/gst_artsio.cc
