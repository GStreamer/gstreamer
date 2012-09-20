lib_LTLIBRARIES = libgstcodecparsers-@GST_API_VERSION@.la

libgstcodecparsers_@GST_API_VERSION@_la_SOURCES = \
	gstmpegvideoparser.c gsth264parser.c gstvc1parser.c gstmpeg4parser.c \
	parserutils.c \
	gstmpegvideometa.c

libgstcodecparsers_@GST_API_VERSION@includedir = \
	$(includedir)/gstreamer-@GST_API_VERSION@/gst/codecparsers

noinst_HEADERS = parserutils.h

libgstcodecparsers_@GST_API_VERSION@include_HEADERS = \
	gstmpegvideoparser.h gsth264parser.h gstvc1parser.h gstmpeg4parser.h \
	gstmpegvideometa.h

libgstcodecparsers_@GST_API_VERSION@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_CFLAGS)

libgstcodecparsers_@GST_API_VERSION@_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)

libgstcodecparsers_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)

Android.mk:  $(BUILT_SOURCES) Makefile.am
	androgenizer -:PROJECT libgstcodecparsers -:STATIC libgstcodecparsers-@GST_API_VERSION@ \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstcodecparsers_@GST_API_VERSION@_la_SOURCES) \
         $(built_sources) \
	 -:CFLAGS $(DEFS) $(libgstcodecparsers_@GST_API_VERSION@_la_CFLAGS) \
	 -:LDFLAGS $(libgstcodecparsers_@GST_API_VERSION@_la_LDFLAGS) \
	           $(libgstcodecparsers@GST_API_VERSION@_la_LIBADD) \
	           -ldl \
	 -:HEADER_TARGET gstreamer-@GST_API_VERSION@/gst/codecparsers \
	 -:HEADERS $(libgstcodecparsersinclude_HEADERS) \
         $(built_headers) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
