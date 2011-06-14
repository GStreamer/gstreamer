# variables used for enum/marshal generation
glib_enum_define = GST_PLAY
glib_gen_prefix = gst_play
glib_gen_basename = gstplay

built_sources = gstplay-marshal.c
built_headers = gstplay-marshal.h

plugin_LTLIBRARIES = libgstplayback.la

libgstplayback_la_SOURCES = \
	gstdecodebin2.c \
	gsturidecodebin.c \
	gstplayback.c \
	gstplaybin2.c \
	gstplaysink.c \
	gstplay-enum.c \
	gststreamselector.c \
	gstsubtitleoverlay.c \
	gstplaysinkvideoconvert.c \
	gstplaysinkaudioconvert.c \
	gststreamsynchronizer.c

nodist_libgstplayback_la_SOURCES = $(built_sources)
libgstplayback_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstplayback_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstplayback_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_MAJORMINOR@.la \
	$(GST_LIBS)
libgstplayback_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstplayback.h \
	gstplaysink.h \
	gstplay-enum.h \
	gststreamselector.h \
	gstrawcaps.h \
	gstsubtitleoverlay.h \
	gstplaysinkvideoconvert.h \
	gstplaysinkaudioconvert.h \
	gststreamsynchronizer.h

BUILT_SOURCES = $(built_headers) $(built_sources)

EXTRA_DIST = gstplay-marshal.list

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/gst-glib-gen.mak

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstplayback -:SHARED libgstplayback \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstplayback_la_SOURCES) \
	           $(nodist_libgstplayback_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstplayback_la_CFLAGS) \
	 -:LDFLAGS $(libgstplayback_la_LDFLAGS) \
	           $(libgstplayback_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
