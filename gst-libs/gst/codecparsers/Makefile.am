lib_LTLIBRARIES = libgstcodecparsers-@GST_API_VERSION@.la

libgstcodecparsers_@GST_API_VERSION@_la_SOURCES = \
	gstmpegvideoparser.c gsth264parser.c gstvc1parser.c gstmpeg4parser.c \
	gsth265parser.c gstvp8parser.c gstvp8rangedecoder.c \
	parserutils.c nalutils.c dboolhuff.c vp8utils.c \
	gstmpegvideometa.c

libgstcodecparsers_@GST_API_VERSION@includedir = \
	$(includedir)/gstreamer-@GST_API_VERSION@/gst/codecparsers

noinst_HEADERS = parserutils.h nalutils.h dboolhuff.h vp8utils.h

libgstcodecparsers_@GST_API_VERSION@include_HEADERS = \
	gstmpegvideoparser.h gsth264parser.h gstvc1parser.h gstmpeg4parser.h \
	gsth265parser.h gstvp8parser.h gstvp8rangedecoder.h \
	gstmpegvideometa.h

libgstcodecparsers_@GST_API_VERSION@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_CFLAGS) \
	-Dvp8_norm=gst_codecparsers_vp8_norm \
	-Dvp8dx_start_decode=gst_codecparsers_vp8dx_start_decode \
	-Dvp8dx_bool_decoder_fill=gst_codecparsers_vp8dx_bool_decoder_fill

libgstcodecparsers_@GST_API_VERSION@_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(LIBM)

libgstcodecparsers_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)

EXTRA_DIST = dboolhuff.LICENSE dboolhuff.PATENTS dboolhuff.AUTHORS

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
