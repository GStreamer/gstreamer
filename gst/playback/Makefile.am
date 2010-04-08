# variables used for enum/marshal generation
glib_enum_define = GST_PLAY
glib_gen_prefix = gst_play
glib_gen_basename = gstplay

built_sources = gstplay-marshal.c
built_headers = gstplay-marshal.h

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstplaybin.la libgstdecodebin.la libgstdecodebin2.la

libgstplaybin_la_SOURCES = \
	gstplayback.c \
	gstplaybin.c \
	gstplaybin2.c \
	gstplaysink.c \
	gstplaybasebin.c \
	gstplay-enum.c \
	gstfactorylists.c \
	gstinputselector.c \
	gstscreenshot.c \
	gststreaminfo.c \
	gststreamselector.c \
	gstsubtitleoverlay.c

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

libgstdecodebin2_la_SOURCES = gstdecodebin2.c gsturidecodebin.c gstfactorylists.c gstplay-enum.c
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
	gstfactorylists.h \
	gstinputselector.h \
	gstplay-enum.h \
	gstscreenshot.h \
	gststreamselector.h \
	gstrawcaps.h \
	gstsubtitleoverlay.h

noinst_PROGRAMS = test decodetest test2 test3 test4 test5 test6 test7

test_LDADD = $(GST_LIBS) 
test_CFLAGS = $(GST_CFLAGS)

test2_LDADD = $(GST_LIBS) 
test2_CFLAGS = $(GST_CFLAGS)

test3_LDADD = $(GST_LIBS) 
test3_CFLAGS = $(GST_CFLAGS)

test4_LDADD = $(GST_LIBS) 
test4_CFLAGS = $(GST_CFLAGS)

test5_LDADD = $(GST_LIBS) 
test5_CFLAGS = $(GST_CFLAGS)

test6_LDADD = $(GST_LIBS) 
test6_CFLAGS = $(GST_CFLAGS)

test7_LDADD = $(GST_LIBS) 
test7_CFLAGS = $(GST_CFLAGS)

decodetest_LDADD = $(GST_LIBS) 
decodetest_CFLAGS = $(GST_CFLAGS)

BUILT_SOURCES = $(built_headers) $(built_sources)

EXTRA_DIST = gstplay-marshal.list

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/gst-glib-gen.mak

