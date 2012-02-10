plugin_LTLIBRARIES = libgstcairo.la

if USE_CAIRO_GOBJECT
glib_enum_define = GST_CAIRO
glib_gen_prefix = gst_cairo
glib_gen_basename = gstcairo

include $(top_srcdir)/common/gst-glib-gen.mak

built_sources = gstcairo-marshal.c
built_headers = gstcairo-marshal.h

BUILT_SOURCES = $(built_sources) $(built_headers)

gstcairo_gobject_dep_sources = gstcairooverlay.c
gstcairo_gobject_dep_headers = gstcairooverlay.h

CLEANFILES = $(BUILT_SOURCES)
endif

noinst_HEADERS = \
	gsttimeoverlay.h \
	gsttextoverlay.h \
	gstcairorender.h \
	$(gstcairo_gobject_dep_headers)
libgstcairo_la_SOURCES = \
	gstcairo.c \
	gsttimeoverlay.c \
	gsttextoverlay.c \
	gstcairorender.c \
	$(gstcairo_gobject_dep_sources)
nodist_libgstcairo_la_SOURCES = \
    $(built_sources)
libgstcairo_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) $(CAIRO_CFLAGS) $(CAIRO_GOBJECT_CFLAGS)
libgstcairo_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(CAIRO_LIBS) $(CAIRO_GOBJECT_LIBS) $(LIBM)
libgstcairo_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcairo_la_LIBTOOLFLAGS = --tag=disable-static

EXTRA_DIST = gstcairo-marshal.list

