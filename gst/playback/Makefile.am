# variables used for enum/marshal generation
glib_enum_define = GST_PLAY
glib_gen_prefix = gst_play
glib_gen_basename = gstplay

built_sources = gstplay-marshal.c
built_headers = gstplay-marshal.h

plugin_LTLIBRARIES = libgstplaybin.la libgstdecodebin.la libgstdecodebin2.la

libgstplaybin_la_SOURCES = \
	gstplayback.c \
	gstplaybin.c \
	gstplaybin2.c \
	gstplaysink.c \
	gstplaybasebin.c \
	gstplay-enum.c \
	gststreaminfo.c \
	gststreamselector.c \
	gstsubtitleoverlay.c \
	gststreamsynchronizer.c

nodist_libgstplaybin_la_SOURCES = $(built_sources)
libgstplaybin_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstplaybin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstplaybin_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_MAJORMINOR@.la \
	$(GST_LIBS)
libgstplaybin_la_LIBTOOLFLAGS = --tag=disable-static

libgstdecodebin_la_SOURCES = gstdecodebin.c
nodist_libgstdecodebin_la_SOURCES = $(built_sources)
libgstdecodebin_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstdecodebin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdecodebin_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la \
	$(GST_LIBS)
libgstdecodebin_la_LIBTOOLFLAGS = --tag=disable-static

libgstdecodebin2_la_SOURCES = gstdecodebin2.c gsturidecodebin.c gstplay-enum.c
nodist_libgstdecodebin2_la_SOURCES = $(built_sources)
libgstdecodebin2_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstdecodebin2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdecodebin2_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la \
	$(GST_LIBS)
libgstdecodebin2_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstplayback.h \
	gstplaybasebin.h \
	gstplaysink.h \
	gststreaminfo.h \
	gstplay-enum.h \
	gststreamselector.h \
	gstrawcaps.h \
	gstsubtitleoverlay.h \
	gststreamsynchronizer.h

BUILT_SOURCES = $(built_headers) $(built_sources)

EXTRA_DIST = gstplay-marshal.list

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/gst-glib-gen.mak

