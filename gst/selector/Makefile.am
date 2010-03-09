glib_gen_prefix = gst_selector
glib_gen_basename = gstselector

include $(top_srcdir)/common/gst-glib-gen.mak

built_sources = gstselector-marshal.c
built_headers = gstselector-marshal.h

BUILT_SOURCES = $(built_sources) $(built_headers)

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = gstselector-marshal.list

plugin_LTLIBRARIES = libgstselector.la

libgstselector_la_SOURCES = gstselector.c gstinputselector.c gstoutputselector.c
nodist_libgstselector_la_SOURCES = $(built_sources)
libgstselector_la_CFLAGS = $(GST_CFLAGS)
libgstselector_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstselector_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstselector_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstinputselector.h gstoutputselector.h 
