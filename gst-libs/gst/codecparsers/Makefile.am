lib_LTLIBRARIES = libgstcodecparsers-@GST_MAJORMINOR@.la

libgstcodecparsers_@GST_MAJORMINOR@_la_SOURCES = gstmpegvideoparser.c

libgstcodecparsers_@GST_MAJORMINOR@includedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/codecparsers

libgstcodecparsers_@GST_MAJORMINOR@include_HEADERS = \
	gstmpegvideoparser.h

libgstcodecparsers_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_CFLAGS)
libgstcodecparsers_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstcodecparsers_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

Android.mk:  $(BUILT_SOURCES) Makefile.am
	androgenizer -:PROJECT libgstcodecparsers -:STATIC libgstcodecparsers-@GST_MAJORMINOR@ \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstcodecparsers_@GST_MAJORMINOR@_la_SOURCES) \
         $(built_sources) \
	 -:CFLAGS $(DEFS) $(libgstcodecparsers_@GST_MAJORMINOR@_la_CFLAGS) \
	 -:LDFLAGS $(libgstcodecparsers_@GST_MAJORMINOR@_la_LDFLAGS) \
	           $(libgstcodecparsers@GST_MAJORMINOR@_la_LIBADD) \
	           -ldl \
	 -:HEADER_TARGET gstreamer-@GST_MAJORMINOR@/gst/codecparsers \
	 -:HEADERS $(libgstcodecparsersinclude_HEADERS) \
         $(built_headers) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
