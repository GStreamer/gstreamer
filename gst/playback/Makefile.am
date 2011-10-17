# variables used for enum/marshal generation
glib_enum_define = GST_PLAY
glib_gen_prefix = gst_play
glib_gen_basename = gstplay

built_sources = gstplay-marshal.c
built_headers = gstplay-marshal.h

plugin_LTLIBRARIES = libgstplaybin.la libgstdecodebin.la libgstdecodebin2.la

csp_cflags = -DCOLORSPACE=\"ffmpegcolorspace\"

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
	gstplaysinkvideoconvert.c \
	gstplaysinkaudioconvert.c \
	gstplaysinkconvertbin.c \
	gststreamsynchronizer.c

nodist_libgstplaybin_la_SOURCES = $(built_sources)
libgstplaybin_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(csp_cflags)
libgstplaybin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstplaybin_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_MAJORMINOR@.la \
	$(GST_LIBS)
libgstplaybin_la_LIBTOOLFLAGS = --tag=disable-static

libgstdecodebin_la_SOURCES = gstdecodebin.c
nodist_libgstdecodebin_la_SOURCES = $(built_sources)
libgstdecodebin_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(csp_cflags)
libgstdecodebin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdecodebin_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la \
	$(GST_LIBS)
libgstdecodebin_la_LIBTOOLFLAGS = --tag=disable-static

libgstdecodebin2_la_SOURCES = gstdecodebin2.c gsturidecodebin.c gstplay-enum.c
nodist_libgstdecodebin2_la_SOURCES = $(built_sources)
libgstdecodebin2_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(csp_cflags)
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
	gstplaysinkvideoconvert.h \
	gstplaysinkaudioconvert.h \
	gstplaysinkconvertbin.h \
	gststreamsynchronizer.h

BUILT_SOURCES = $(built_headers) $(built_sources)

EXTRA_DIST = gstplay-marshal.list

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/gst-glib-gen.mak

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstdecodebin -:SHARED libgstdecodebin \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstdecodebin_la_SOURCES) \
	           $(nodist_libgstdecodebin_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstdecodebin_la_CFLAGS) $(csp_cflags) \
	 -:LDFLAGS $(libgstdecodebin_la_LDFLAGS) \
	           $(libgstdecodebin_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	 \
	-:PROJECT libgstdecodebin2 -:SHARED libgstdecodebin2 \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstdecodebin2_la_SOURCES) \
	           $(nodist_libgstdecodebin2_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstdecodebin2_la_CFLAGS) $(csp_cflags) \
	 -:LDFLAGS $(libgstdecodebin2_la_LDFLAGS) \
	           $(libgstdecodebin2_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	 \
	-:PROJECT libgstplaybin -:SHARED libgstplaybin \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstplaybin_la_SOURCES) \
	           $(nodist_libgstplaybin_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstplaybin_la_CFLAGS) $(csp_cflags) \
	 -:LDFLAGS $(libgstplaybin_la_LDFLAGS) \
	           $(libgstplaybin_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
