## we wrap this Makefile.am in a USE_ so that we won't run mcopidl
## during make distcheck when it isn't on the system
if USE_ARTS
plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstarts.la

gst_artsio_impl.lo: gst_artsio.cc

SUFFIXES = .idl
.idl.cc:
	mcopidl -t $< $(ARTS_CXXFLAGS)

# mcopidl Extension Expansion Technology clean up
CLEANFILES = gst_artsio.h gst_artsio.cc gst_artsio.mcopclass gst_artsio.mcoptype

libgstarts_la_SOURCES = gst_arts.c gst_artsio.cc gst_artsio_impl.cc
noinst_HEADERS = gst_arts.h gst_artsio_impl.h

# FIXME automake 1.4 hack, 1.5 should let us put the .idl in
#       _SOURCES at which point the follow can be removed
EXTRA_DIST = gst_artsio.idl
dist-hook:
	rm -f $(distdir)/gst_artsio.cc

libgstarts_la_CFLAGS = $(GST_CFLAGS)
libgstarts_la_CXXFLAGS = $(ARTS_CXXFLAGS) $(GST_CFLAGS)
libgstarts_la_LIBADD = $(ARTS_LIBS)
endif
