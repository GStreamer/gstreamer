glib_enum_define=GST_PLAY
glib_enum_prefix=gst_play

built_sources = gstplay-marshal.c

built_headers = gstplay-marshal.h

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstplaybin.la libgstdecodebin.la libgstdecodebin2.la libgstqueue2.la \
		     libgsturidecodebin.la 

libgstplaybin_la_SOURCES = \
	gstplayback.c \
	gstplaybin.c \
	gstplaybin2.c \
	gstplaysink.c \
	gstplaybasebin.c \
	gststreaminfo.c \
	gststreamselector.c

nodist_libgstplaybin_la_SOURCES = $(built_sources)
libgstplaybin_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstplaybin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstplaybin_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la \
	$(GST_LIBS)

libgstdecodebin_la_SOURCES = gstdecodebin.c
nodist_libgstdecodebin_la_SOURCES = $(built_sources)
libgstdecodebin_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstdecodebin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdecodebin_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la \
	$(GST_LIBS)

libgstdecodebin2_la_SOURCES = gstdecodebin2.c
nodist_libgstdecodebin2_la_SOURCES = $(built_sources)
libgstdecodebin2_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstdecodebin2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdecodebin2_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la \
	$(GST_LIBS)

libgstqueue2_la_SOURCES = gstqueue2.c
nodist_libgstqueue2_la_SOURCES = $(built_sources)
libgstqueue2_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstqueue2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstqueue2_la_LIBADD = $(GST_LIBS)

libgsturidecodebin_la_SOURCES = gsturidecodebin.c
nodist_libgsturidecodebin_la_SOURCES = $(built_sources)
libgsturidecodebin_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgsturidecodebin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsturidecodebin_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la \
	$(GST_LIBS)

noinst_HEADERS = \
	gstplaybasebin.h \
	gstplaysink.h \
	gststreaminfo.h \
	gststreamselector.h

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

include $(top_srcdir)/common/glib-gen.mak
