plugin_LTLIBRARIES = libgstmpegaudioparse.la

libgstmpegaudioparse_la_SOURCES = plugin.c gstmpegaudioparse.c gstxingmux.c
libgstmpegaudioparse_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmpegaudioparse_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstmpegaudioparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpegaudioparse_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstmpegaudioparse.h gstxingmux.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstmpegaudioparse -:SHARED libgstmpegaudioparse \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmpegaudioparse_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstmpegaudioparse_la_CFLAGS) \
	 -:LDFLAGS $(libgstmpegaudioparse_la_LDFLAGS) \
	           $(libgstmpegaudioparse_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
