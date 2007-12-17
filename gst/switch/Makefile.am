glib_enum_prefix = gst_switch

include $(top_srcdir)/common/glib-gen.mak

built_sources = gstswitch-marshal.c
built_headers = gstswitch-marshal.h

BUILT_SOURCES = $(built_sources) $(built_headers)

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = gstswitch-marshal.list

plugin_LTLIBRARIES = libgstswitch.la

libgstswitch_la_SOURCES = gstswitch.c
nodist_libgstswitch_la_SOURCES = $(built_sources)
libgstswitch_la_CFLAGS = $(GST_CFLAGS)
libgstswitch_la_LIBADD =
libgstswitch_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS)

noinst_HEADERS = gstswitch.h $(built_headers)
