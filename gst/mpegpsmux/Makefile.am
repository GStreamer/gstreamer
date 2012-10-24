plugin_LTLIBRARIES = libgstmpegpsmux.la

libgstmpegpsmux_la_SOURCES = \
	mpegpsmux.c \
	psmux.c \
	psmuxstream.c \
	mpegpsmux_aac.c \
	mpegpsmux_h264.c

libgstmpegpsmux_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmpegpsmux_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstmpegpsmux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpegpsmux_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	mpegpsmux.h \
	psmux.h \
	psmuxstream.h \
	psmuxcommon.h \
	mpegpsmux_aac.h \
	mpegpsmux_h264.h \
	bits.h \
	crc.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstmpegpsmux -:SHARED libgstmpegpsmux \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmpegpsmux_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstmpegpsmux_la_CFLAGS) \
	 -:LDFLAGS $(libgstmpegpsmux_la_LDFLAGS) \
	           $(libgstmpegpsmux_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
