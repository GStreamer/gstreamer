filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgst_arts.la

gst_artsio_impl.lo: gst_artsio.cc

SUFFIXES = .idl
.idl.cc:
	mcopidl -t $< $(ARTS_MCOPFLAGS)

# mcopidl Extension Expansion Technology clean up
CLEANFILES = gst_artsio.h gst_artsio.cc gst_artsio.mcopclass gst_artsio.mcoptype

libgst_arts_la_SOURCES = gst_arts.c gst_artsio.cc gst_artsio_impl.cc
noinst_HEADERS = gst_arts.h gst_artsio_impl.h

# FIXME automake 1.4 hack, 1.5 should let us put the .idl in
#       _SOURCES at which point the follow can be removed
EXTRA_DIST = gst_artsio.idl
dist-hook:
	rm -f $(distdir)/gst_artsio.cc

# gst_artsio.cc and gst_artsio.h are generated from the idl, and the tools
# needed to do this should be present on any platform where the rest of arts
# is present: therefore, these don't need to go in the dist.
#EXTRA_DIST = gst_artsio.cc gst_artsio.h

libgst_arts_la_CFLAGS = $(ARTS_CFLAGS)
libgst_arts_la_CXXFLAGS = $(ARTS_CFLAGS) $(CFLAGS)
libgst_arts_la_LDFLAGS = $(ARTS_LIBS)
