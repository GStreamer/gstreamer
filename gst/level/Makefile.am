plugin_LTLIBRARIES = libgstlevel.la

# variables used for enum/marshal generation
glib_enum_define = GST_LEVEL
glib_enum_prefix = gst_level

include $(top_srcdir)/common/glib-gen.mak
                                                                                
built_sources = gstlevel-marshal.c
built_headers = gstlevel-marshal.h
                                                                                
BUILT_SOURCES = $(built_sources) $(built_headers)

libgstlevel_la_SOURCES = gstlevel.c
nodist_libgstlevel_la_SOURCES = $(built_sources)
libgstlevel_la_CFLAGS = $(GST_CFLAGS)
libgstlevel_la_LIBADD =
libgstlevel_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstlevel.h filter.func

if HAVE_GTK
noinst_PROGRAMS = demo plot
demo_SOURCES = demo.c
demo_CFLAGS = $(GTK_CFLAGS) $(GST_CFLAGS)
demo_LDFLAGS = $(GTK_LIBS) $(GST_LIBS)
plot_SOURCES = plot.c
plot_CFLAGS = $(GTK_CFLAGS) $(GST_CFLAGS)
plot_LDFLAGS = $(GTK_LIBS) $(GST_LIBS)
endif

CLEANFILES = $(BUILT_SOURCES)
                                                                        
EXTRA_DIST = README gst-level-marshal.list
