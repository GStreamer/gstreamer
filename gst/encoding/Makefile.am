# variables used for enum/marshal generation
glib_enum_define = GST_ENCODE
glib_gen_prefix = gst_encode
glib_gen_basename = gstencode

built_sources = gstencode-marshal.c
built_headers = gstencode-marshal.h

plugin_LTLIBRARIES = libgstencodebin.la

libgstencodebin_la_SOURCES = 	\
	gstencodebin.c		\
	gstsmartencoder.c	\
	gststreamcombiner.c	\
	gststreamsplitter.c

nodist_libgstencodebin_la_SOURCES = $(built_sources)
libgstencodebin_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstencodebin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstencodebin_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la \
	$(GST_LIBS)
libgstencodebin_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = 		\
	gstencodebin.h		\
	gststreamcombiner.h	\
	gststreamsplitter.h	\
	gstsmartencoder.h


BUILT_SOURCES = $(built_headers) $(built_sources)

EXTRA_DIST = gstencode-marshal.list

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/gst-glib-gen.mak

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstencodebin -:SHARED libgstencodebin \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstencodebin_la_SOURCES) \
	 	   $(nodist_libgstencodebin_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstencodebin_la_CFLAGS) \
	 -:LDFLAGS $(libgstencodebin_la_LDFLAGS) \
	           $(libgstencodebin_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
